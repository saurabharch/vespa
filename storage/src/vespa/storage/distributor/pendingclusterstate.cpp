// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "pendingclusterstate.h"
#include "pending_bucket_space_db_transition.h"
#include "bucketdbupdater.h"
#include "distributor_bucket_space_repo.h"
#include "distributor_bucket_space.h"
#include <vespa/storageframework/defaultimplementation/clock/realclock.h>
#include <vespa/storage/common/bucketoperationlogger.h>
#include <vespa/document/bucket/fixed_bucket_spaces.h>
#include <vespa/vespalib/util/xmlstream.hpp>
#include <climits>

#include <vespa/log/log.h>
LOG_SETUP(".pendingclusterstate");

using document::BucketSpace;

namespace storage::distributor {

using lib::Node;
using lib::NodeType;
using lib::NodeState;

PendingClusterState::PendingClusterState(
        const framework::Clock& clock,
        const ClusterInformation::CSP& clusterInfo,
        DistributorMessageSender& sender,
        DistributorBucketSpaceRepo &bucketSpaceRepo,
        const std::shared_ptr<api::SetSystemStateCommand>& newStateCmd,
        const OutdatedNodesMap &outdatedNodesMap,
        api::Timestamp creationTimestamp)
    : _cmd(newStateCmd),
      _requestedNodes(newStateCmd->getSystemState().getNodeCount(lib::NodeType::STORAGE)),
      _prevClusterStateBundle(clusterInfo->getClusterStateBundle()),
      _newClusterStateBundle(newStateCmd->getClusterStateBundle()),
      _clock(clock),
      _clusterInfo(clusterInfo),
      _creationTimestamp(creationTimestamp),
      _sender(sender),
      _bucketSpaceRepo(bucketSpaceRepo),
      _bucketOwnershipTransfer(false),
      _pendingTransitions()
{
    logConstructionInformation();
    initializeBucketSpaceTransitions(false, outdatedNodesMap);
}

PendingClusterState::PendingClusterState(
        const framework::Clock& clock,
        const ClusterInformation::CSP& clusterInfo,
        DistributorMessageSender& sender,
        DistributorBucketSpaceRepo &bucketSpaceRepo,
        api::Timestamp creationTimestamp)
    : _requestedNodes(clusterInfo->getStorageNodeCount()),
      _prevClusterStateBundle(clusterInfo->getClusterStateBundle()),
      _newClusterStateBundle(clusterInfo->getClusterStateBundle()),
      _clock(clock),
      _clusterInfo(clusterInfo),
      _creationTimestamp(creationTimestamp),
      _sender(sender),
      _bucketSpaceRepo(bucketSpaceRepo),
      _bucketOwnershipTransfer(true),
      _pendingTransitions()
{
    logConstructionInformation();
    initializeBucketSpaceTransitions(true, OutdatedNodesMap());
}

PendingClusterState::~PendingClusterState() {}

void
PendingClusterState::initializeBucketSpaceTransitions(bool distributionChanged, const OutdatedNodesMap &outdatedNodesMap)
{
    OutdatedNodes emptyOutdatedNodes;
    for (auto &elem : _bucketSpaceRepo) {
        auto onItr = outdatedNodesMap.find(elem.first);
        const auto &outdatedNodes = (onItr == outdatedNodesMap.end()) ? emptyOutdatedNodes : onItr->second;
        auto pendingTransition =
            std::make_unique<PendingBucketSpaceDbTransition>
            (*this, *elem.second, distributionChanged, outdatedNodes,
             _clusterInfo, *_newClusterStateBundle.getDerivedClusterState(elem.first), _creationTimestamp);
        if (pendingTransition->getBucketOwnershipTransfer()) {
            _bucketOwnershipTransfer = true;
        }
        _pendingTransitions.emplace(elem.first, std::move(pendingTransition));
    }
    if (shouldRequestBucketInfo()) {
        requestNodes();
    }
}

void
PendingClusterState::logConstructionInformation() const
{
    const auto &distributorBucketSpace(_bucketSpaceRepo.get(document::FixedBucketSpaces::default_space()));
    const auto &distribution(distributorBucketSpace.getDistribution());
    LOG(debug,
        "New PendingClusterState constructed with previous cluster "
        "state '%s', new cluster state '%s', distribution config "
        "hash: '%s'",
        getPrevClusterStateBundleString().c_str(),
        getNewClusterStateBundleString().c_str(),
        distribution.getNodeGraph().getDistributionConfigHash().c_str());
}

bool
PendingClusterState::storageNodeUpInNewState(document::BucketSpace bucketSpace, uint16_t node) const
{
    return _newClusterStateBundle.getDerivedClusterState(bucketSpace)->getNodeState(Node(NodeType::STORAGE, node))
               .getState().oneOf(_clusterInfo->getStorageUpStates());
}

PendingClusterState::OutdatedNodesMap
PendingClusterState::getOutdatedNodesMap() const
{
    OutdatedNodesMap outdatedNodesMap;
    for (const auto &elem : _pendingTransitions) {
        outdatedNodesMap.emplace(elem.first, elem.second->getOutdatedNodes());
    }
    return outdatedNodesMap;
}

uint16_t
PendingClusterState::newStateStorageNodeCount() const
{
    return _newClusterStateBundle.getBaselineClusterState()->getNodeCount(lib::NodeType::STORAGE);
}

bool
PendingClusterState::shouldRequestBucketInfo() const
{
    if (clusterIsDown()) {
        LOG(debug, "Received system state where the cluster is down");
        return false;
    }
    if (iAmDown()) {
        LOG(debug, "Received system state where our node is down");
        return false;
    }
    return true;
}

bool
PendingClusterState::clusterIsDown() const
{
    return _newClusterStateBundle.getBaselineClusterState()->getClusterState() == lib::State::DOWN;
}

bool
PendingClusterState::iAmDown() const
{
    const lib::NodeState& myState(
            _newClusterStateBundle.getBaselineClusterState()->getNodeState(Node(NodeType::DISTRIBUTOR,
                    _sender.getDistributorIndex())));
    return myState.getState() == lib::State::DOWN;
}

void
PendingClusterState::requestNodes()
{
    LOG(debug,
        "New system state: Old state was %s, new state is %s",
        getPrevClusterStateBundleString().c_str(),
        getNewClusterStateBundleString().c_str());

    requestBucketInfoFromStorageNodesWithChangedState();
}

void
PendingClusterState::requestBucketInfoFromStorageNodesWithChangedState()
{
    for (auto &elem : _pendingTransitions) {
        const OutdatedNodes &outdatedNodes(elem.second->getOutdatedNodes());
        for (uint16_t idx : outdatedNodes) {
            if (storageNodeUpInNewState(elem.first, idx)) {
                requestNode(BucketSpaceAndNode(elem.first, idx));
            }
        }
    }
}

void
PendingClusterState::requestNode(BucketSpaceAndNode bucketSpaceAndNode)
{
    const auto &distributorBucketSpace(_bucketSpaceRepo.get(bucketSpaceAndNode.bucketSpace));
    const auto &distribution(distributorBucketSpace.getDistribution());
    vespalib::string distributionHash(distribution.getNodeGraph().getDistributionConfigHash());
    LOG(debug,
        "Requesting bucket info for bucket space %" PRIu64 " node %d with cluster state '%s' "
        "and distribution hash '%s'",
        bucketSpaceAndNode.bucketSpace.getId(),
        bucketSpaceAndNode.node,
        getNewClusterStateBundleString().c_str(),
        distributionHash.c_str());

    std::shared_ptr<api::RequestBucketInfoCommand> cmd(
            new api::RequestBucketInfoCommand(
                    bucketSpaceAndNode.bucketSpace,
                    _sender.getDistributorIndex(),
                    *_newClusterStateBundle.getDerivedClusterState(bucketSpaceAndNode.bucketSpace),
                    distributionHash));

    cmd->setPriority(api::StorageMessage::HIGH);
    cmd->setTimeout(INT_MAX);

    _sentMessages.emplace(cmd->getMsgId(), bucketSpaceAndNode);

    _sender.sendToNode(NodeType::STORAGE, bucketSpaceAndNode.node, cmd);
}


PendingClusterState::Summary::Summary(const std::string& prevClusterState,
                                      const std::string& newClusterState,
                                      uint32_t processingTime)
    : _prevClusterState(prevClusterState),
      _newClusterState(newClusterState),
      _processingTime(processingTime)
{}

PendingClusterState::Summary::Summary(const Summary &) = default;
PendingClusterState::Summary & PendingClusterState::Summary::operator = (const Summary &) = default;
PendingClusterState::Summary::~Summary() { }

bool
PendingClusterState::onRequestBucketInfoReply(const std::shared_ptr<api::RequestBucketInfoReply>& reply)
{
    auto iter = _sentMessages.find(reply->getMsgId());

    if (iter == _sentMessages.end()) {
        return false;
    }
    const BucketSpaceAndNode bucketSpaceAndNode = iter->second;

    api::ReturnCode result(reply->getResult());
    if (result == api::ReturnCode::Result::ENCODE_ERROR) {
        // Handle failure to encode bucket space due to use of old storage api
        // protocol.  Pretend that request succeeded with no buckets returned.
        // TODO remove this workaround for Vespa 7
        LOG(debug, "Got ENCODE_ERROR, pretending success with no buckets");
    } else if (!result.success()) {
        framework::MilliSecTime resendTime(_clock);
        resendTime += framework::MilliSecTime(100);
        _delayedRequests.emplace_back(resendTime, bucketSpaceAndNode);
        _sentMessages.erase(iter);
        return true;
    }

    setNodeReplied(bucketSpaceAndNode.node);
    auto transitionIter = _pendingTransitions.find(bucketSpaceAndNode.bucketSpace);
    assert(transitionIter != _pendingTransitions.end());
    transitionIter->second->onRequestBucketInfoReply(*reply, bucketSpaceAndNode.node);
    _sentMessages.erase(iter);

    return true;
}

void
PendingClusterState::resendDelayedMessages() {
    if (_delayedRequests.empty()) return; // Don't fetch time if not needed
    framework::MilliSecTime currentTime(_clock);
    while (!_delayedRequests.empty()
           && currentTime >= _delayedRequests.front().first)
    {
        requestNode(_delayedRequests.front().second);
        _delayedRequests.pop_front();
    }
}

std::string
PendingClusterState::requestNodesToString() const
{
    std::ostringstream ost;
    for (uint32_t i = 0; i < _requestedNodes.size(); ++i) {
        if (_requestedNodes[i]) {
            if (ost.str().length() > 0) {
                ost << ",";
            }
            ost << i;
        }
    }
    return ost.str();
}

void
PendingClusterState::mergeIntoBucketDatabases()
{
    for (auto &elem : _pendingTransitions) {
        elem.second->mergeIntoBucketDatabase();
    }
}

void
PendingClusterState::printXml(vespalib::XmlOutputStream& xos) const
{
    using namespace vespalib::xml;
    xos << XmlTag("systemstate_pending")
        << XmlAttribute("state", *_newClusterStateBundle.getBaselineClusterState());
    for (auto &elem : _sentMessages) {
        xos << XmlTag("pending")
            << XmlAttribute("node", elem.second.node)
            << XmlEndTag();
    }
    xos << XmlEndTag();
}

PendingClusterState::Summary
PendingClusterState::getSummary() const
{
    return Summary(getPrevClusterStateBundleString(),
                   getNewClusterStateBundleString(),
                   (_clock.getTimeInMicros().getTime() - _creationTimestamp));
}

PendingBucketSpaceDbTransition &
PendingClusterState::getPendingBucketSpaceDbTransition(document::BucketSpace bucketSpace)
{
    auto transitionIter = _pendingTransitions.find(bucketSpace);
    assert(transitionIter != _pendingTransitions.end());
    return *transitionIter->second;
}

}
