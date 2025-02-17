// Copyright Yahoo. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.config;

import org.junit.jupiter.api.Test;

import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * @author bratseth
 */
public class ModelNodeTest {

    @Test
    void testEmpty() {
        assertEquals("(null)", new ModelNode().toString());
    }

    @Test
    void testReference() {
        var reference = new ModelReference(Optional.of("myModelId"),
                                           Optional.of(new UrlReference("https://host:my/path")),
                                           Optional.of(new FileReference("foo.txt")));
        assertEquals("myModelId https://host:my/path foo.txt", reference.toString());
        assertEquals(reference, ModelReference.valueOf(reference.toString()));
    }

}
