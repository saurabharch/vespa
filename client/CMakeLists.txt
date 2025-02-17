# Copyright Yahoo. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

set(GODIR ${CMAKE_CURRENT_SOURCE_DIR}/go)

file(GLOB_RECURSE GOSRCFILES ${GODIR}/*.go)

add_custom_command(OUTPUT ${GODIR}/bin/vespa-logfmt ${GODIR}/bin/vespa-deploy
                   COMMAND make
                   DEPENDS ${GODIR}/Makefile ${GOSRCFILES}
                   WORKING_DIRECTORY ${GODIR})

add_custom_target(vespalog_logfmt ALL DEPENDS ${GODIR}/bin/vespa-logfmt)

install(PROGRAMS ${GODIR}/bin/script-utils DESTINATION libexec/vespa)
install_symlink(libexec/vespa/script-utils bin/vespa-logfmt)
install_symlink(libexec/vespa/script-utils bin/vespa-deploy)
