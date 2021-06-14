// Copyright Verizon Media. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.vespa.filedistribution;

import com.yahoo.config.FileReference;
import java.util.logging.Level;
import com.yahoo.vespa.config.ConnectionPool;
import com.yahoo.vespa.defaults.Defaults;
import com.yahoo.yolean.Exceptions;

import java.io.File;
import java.time.Duration;

import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.logging.Logger;

/**
 * Handles downloads of files (file references only for now)
 *
 * @author hmusum
 */
public class FileDownloader implements AutoCloseable {

    private final static Logger log = Logger.getLogger(FileDownloader.class.getName());
    public static File defaultDownloadDirectory = new File(Defaults.getDefaults().underVespaHome("var/db/vespa/filedistribution"));

    private final ConnectionPool connectionPool;
    private final File downloadDirectory;
    private final Duration timeout;
    private final FileReferenceDownloader fileReferenceDownloader;
    private final Downloads downloads;

    public FileDownloader(ConnectionPool connectionPool) {
        this(connectionPool, defaultDownloadDirectory, new Downloads());
    }

    public FileDownloader(ConnectionPool connectionPool, File downloadDirectory, Downloads downloads) {
        // TODO: Reduce timeout even more, timeout is so long that we might get starvation
        this(connectionPool, downloadDirectory, downloads, Duration.ofMinutes(5), Duration.ofSeconds(10));
    }

    public FileDownloader(ConnectionPool connectionPool, File downloadDirectory, Downloads downloads,
                          Duration timeout, Duration sleepBetweenRetries) {
        this.connectionPool = connectionPool;
        this.downloadDirectory = downloadDirectory;
        this.timeout = timeout;
        // Needed to receive RPC calls receiveFile* from server after asking for files
        new FileReceiver(connectionPool.getSupervisor(), downloads, downloadDirectory);
        this.fileReferenceDownloader = new FileReferenceDownloader(connectionPool, downloads, timeout, sleepBetweenRetries);
        this.downloads = downloads;
    }

    public Optional<File> getFile(FileReference fileReference) {
        return getFile(new FileReferenceDownload(fileReference));
    }

    public Optional<File> getFile(FileReferenceDownload fileReferenceDownload) {
        try {
            return getFutureFile(fileReferenceDownload).get(timeout.toMillis(), TimeUnit.MILLISECONDS);
        } catch (InterruptedException | ExecutionException | TimeoutException e) {
            log.log(Level.WARNING, "Failed downloading '" + fileReferenceDownload +
                                   "', removing from download queue: " + Exceptions.toMessageString(e));
            fileReferenceDownloader.failedDownloading(fileReferenceDownload.fileReference());
            return Optional.empty();
        }
    }

    Future<Optional<File>> getFutureFile(FileReferenceDownload fileReferenceDownload) {
        FileReference fileReference = fileReferenceDownload.fileReference();
        Objects.requireNonNull(fileReference, "file reference cannot be null");

        Optional<File> file = getFileFromFileSystem(fileReference);
        return (file.isPresent())
                ? CompletableFuture.completedFuture(file)
                : download(fileReferenceDownload);
    }

    public Map<FileReference, Double> downloadStatus() { return downloads.downloadStatus(); }

    public ConnectionPool connectionPool() { return connectionPool; }

    File downloadDirectory() {
        return downloadDirectory;
    }

    // Files are moved atomically, so if file reference exists and is accessible we can use it
    private Optional<File> getFileFromFileSystem(FileReference fileReference) {
        File[] files = new File(downloadDirectory, fileReference.value()).listFiles();
        if (downloadDirectory.exists() && downloadDirectory.isDirectory() && files != null && files.length > 0) {
            File file = files[0];
            if (!file.exists()) {
                throw new RuntimeException("File reference '" + fileReference.value() + "' does not exist");
            } else if (!file.canRead()) {
                throw new RuntimeException("File reference '" + fileReference.value() + "' exists, but unable to read it");
            } else {
                log.log(Level.FINE, () -> "File reference '" + fileReference.value() + "' found: " + file.getAbsolutePath());
                downloads.setDownloadStatus(fileReference, 1.0);
                return Optional.of(file);
            }
        }
        return Optional.empty();
    }

    boolean isDownloading(FileReference fileReference) {
        return downloads.get(fileReference).isPresent();
    }

    private boolean alreadyDownloaded(FileReferenceDownload fileReferenceDownload) {
        try {
            return getFileFromFileSystem(fileReferenceDownload.fileReference()).isPresent();
        } catch (RuntimeException e) {
            return false;
        }
    }

    /** Start a download, don't wait for result */
    public void downloadIfNeeded(FileReferenceDownload fileReferenceDownload) {
        if (alreadyDownloaded(fileReferenceDownload)) return;

        download(fileReferenceDownload);
    }

    /** Download, the future returned will be complete()d by receiving method in {@link FileReceiver} */
    private synchronized Future<Optional<File>> download(FileReferenceDownload fileReferenceDownload) {
        return fileReferenceDownloader.download(fileReferenceDownload);
    }

    public void close() {
        fileReferenceDownloader.close();
    }

}
