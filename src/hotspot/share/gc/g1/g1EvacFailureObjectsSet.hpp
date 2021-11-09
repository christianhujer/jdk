/*
 * Copyright (c) 2021, Huawei and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_GC_G1_G1EVACUATIONFAILUREOBJSINHR_HPP
#define SHARE_GC_G1_G1EVACUATIONFAILUREOBJSINHR_HPP

#include "gc/g1/g1SegmentedArray.hpp"
#include "memory/allocation.hpp"
#include "memory/iterator.hpp"
#include "oops/oop.hpp"

// This class collects addresses of objects that failed evacuation in a specific
// heap region.
// Provides sorted iteration of these elements for processing during the remove
// self forwards phase.
class G1EvacFailureObjectsSet {
public:
  // Storage type of an object that failed evacuation within a region. Given
  // heap region size and possible object locations within a region, it is
  // sufficient to use an uint here to save some space instead of full pointers.
  typedef uint OffsetInRegion;

private:
  // Helper class to join, sort and iterate over the previously collected segmented
  // array of objects that failed evacuation.
  class G1EvacFailureObjectsIterationHelper {
    typedef G1EvacFailureObjectsSet::OffsetInRegion OffsetInRegion;

    G1EvacFailureObjectsSet* _objects_set;
    const G1SegmentedArray<OffsetInRegion, mtGC>* _segments;
    OffsetInRegion* _offset_array;
    uint _array_length;
    uint _num_allocated_nodes;

    static int order_oop(OffsetInRegion a, OffsetInRegion b);

    void join_and_sort();

    void iterate_internal(ObjectClosure* closure) const;

  public:
    G1EvacFailureObjectsIterationHelper(G1EvacFailureObjectsSet* collector);

    void pre_iterate();
    void iterate(ObjectClosure* closure) const;
    void post_iterate();

    // Callback of G1SegmentedArray::iterate_nodes
    void do_buffer(G1SegmentedArrayBuffer<mtGC>* node, uint length);
  };

private:
  static const uint BufferLength = 256;
  static const uint Alignment = 4;

  static const G1SegmentedArrayAllocOptions _alloc_options;

  // This free list is shared among evacuation failure process in all regions.
  static G1SegmentedArrayBufferList<mtGC> _free_buffer_list;

  DEBUG_ONLY(const uint _region_idx;)

  // Region bottom
  const HeapWord* _bottom;

  // Offsets within region containing objects that failed evacuation.
  G1SegmentedArray<OffsetInRegion, mtGC> _offsets;

  G1EvacFailureObjectsIterationHelper _helper;

  void assert_is_valid_offset(size_t offset) const NOT_DEBUG_RETURN;
  // Converts between an offset within a region and an oop address.
  oop from_offset(OffsetInRegion offset) const;
  OffsetInRegion to_offset(oop obj) const;

public:
  G1EvacFailureObjectsSet(uint region_idx, HeapWord* bottom);

  // Record an object that failed evacuation.
  void record(oop obj);

  void pre_iterate();
  // Apply the given ObjectClosure to all objects that failed evacuation. Objects
  // are passed in increasing address order.
  void iterate(ObjectClosure* closure) const;
  void post_iterate();
};

#endif //SHARE_GC_G1_G1EVACUATIONFAILUREOBJSINHR_HPP
