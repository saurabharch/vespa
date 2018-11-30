// Copyright 2018 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.vespa.service.monitor.application;

import com.yahoo.config.provision.ClusterSpec;
import com.yahoo.config.provision.NodeType;
import com.yahoo.vespa.applicationmodel.ServiceType;

/**
 * @author mpolden
 */
public class ControllerApplication extends InfraApplication {
    public ControllerApplication() {
        super("controller", NodeType.controller, ClusterSpec.Type.container,
                ClusterSpec.Id.from("controller"), ServiceType.CONTROLLER);
    }

}
