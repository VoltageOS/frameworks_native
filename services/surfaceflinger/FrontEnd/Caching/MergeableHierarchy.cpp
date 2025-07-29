/*
 * Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <FrontEnd/LayerHierarchy.h>
#include <iterator>
#include "FrontEnd/LayerSnapshot.h"

#include "MergeableHierarchy.h"

namespace android::surfaceflinger::frontend::caching {

bool MergeableHierarchy::Accumulator::add(const LayerHierarchy* hierarchy) {
    // TODO: Add a check for whether we actually want to add the hierarchy
    // For now, unconditionally add the hierarchy
    mHierarchies.push_back(
            {.layerId = hierarchy->getLayer() ? hierarchy->getLayer()->id : UNASSIGNED_LAYER_ID,
             .hierarchy = hierarchy});
    return true;
}

void MergeableHierarchy::constructSnapshot(LayerSnapshotBuilder& builder,
                                           const LayerSnapshotBuilder::Args& args) {
    if (mSnapshot) {
        return;
    }

    auto localArgs = args;
    localArgs.forceUpdate = LayerSnapshotBuilder::ForceUpdateFlags::ALL;

    std::vector<LayerSnapshot> snapshots;
    constructSnapshotForHierarchy(builder, localArgs, mHierarchies.front().hierarchy,
                                  localArgs.rootSnapshot, snapshots);

    ALOGD("Constructed %zu snapshots!", snapshots.size());
}

void MergeableHierarchy::constructSnapshotForHierarchy(LayerSnapshotBuilder& builder,
                                                       const LayerSnapshotBuilder::Args& args,
                                                       const LayerHierarchy* hierarchy,
                                                       const LayerSnapshot& parent,
                                                       std::vector<LayerSnapshot>& outSnapshots) {
    auto snapshot = !hierarchy->getLayer()
            ? args.rootSnapshot
            : LayerSnapshot(*hierarchy->getLayer(), LayerHierarchy::TraversalPath::ROOT);

    if (hierarchy->getLayer()) {
        builder.updateSnapshot(snapshot, args, *hierarchy->getLayer(), parent,
                               LayerHierarchy::TraversalPath::ROOT);
    }

    std::vector<LayerSnapshot> children;
    for (const auto& [child, _] : hierarchy->mChildren) {
        constructSnapshotForHierarchy(builder, args, child, snapshot, children);
    }

    outSnapshots.emplace_back(std::move(snapshot));
    std::move(children.begin(), children.end(), std::back_inserter(outSnapshots));
}

void MergeableHierarchy::dump(std::ostream& out) const {
    out << "id = " << getId() << ", hierarchies = {";
    for (const auto& hierarchy : mHierarchies) {
        out << hierarchy.layerId << ",";
    }
    out << "}";
}

} // namespace android::surfaceflinger::frontend::caching