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

#include "precompiled.hpp"
#include "gc/g1/g1EvacFailureObjectsSet.hpp"
#include "gc/g1/g1CollectedHeap.hpp"
#include "gc/g1/g1SegmentedArray.inline.hpp"
#include "gc/g1/heapRegion.hpp"
#include "gc/g1/heapRegion.inline.hpp"
#include "utilities/quickSort.hpp"


const G1SegmentedArrayAllocOptions G1EvacFailureObjectsSet::_alloc_options =
  G1SegmentedArrayAllocOptions((uint)sizeof(OffsetInRegion), BufferLength, UINT_MAX, Alignment);

G1SegmentedArrayBufferList<mtGC> G1EvacFailureObjectsSet::_free_buffer_list;

#ifdef ASSERT
void G1EvacFailureObjectsSet::assert_is_valid_offset(size_t offset) const {
  const uint max_offset = 1u << (HeapRegion::LogOfHRGrainBytes - LogHeapWordSize);
  assert(offset < max_offset, "must be, but is " SIZE_FORMAT, offset);
}
#endif

oop G1EvacFailureObjectsSet::from_offset(OffsetInRegion offset) const {
  assert_is_valid_offset(offset);
  return cast_to_oop(_bottom + offset);
}

G1EvacFailureObjectsSet::OffsetInRegion G1EvacFailureObjectsSet::to_offset(oop obj) const {
  const HeapWord* o = cast_from_oop<const HeapWord*>(obj);
  size_t offset = pointer_delta(o, _bottom);
  assert(obj == from_offset(static_cast<OffsetInRegion>(offset)), "must be");
  return static_cast<OffsetInRegion>(offset);
}

G1EvacFailureObjectsSet::G1EvacFailureObjectsSet(uint region_idx, HeapWord* bottom) :
  DEBUG_ONLY(_region_idx(region_idx) COMMA)
  _bottom(bottom),
  _offsets(&_alloc_options, &_free_buffer_list),
  _helper(this) {
  assert(HeapRegion::LogOfHRGrainBytes < 32, "must be");
}

void G1EvacFailureObjectsSet::record(oop obj) {
  assert(obj != NULL, "must be");
  assert(_region_idx == G1CollectedHeap::heap()->heap_region_containing(obj)->hrm_index(), "must be");
  OffsetInRegion* e = _offsets.allocate();
  *e = to_offset(obj);
}

void G1EvacFailureObjectsSet::pre_iterate() {
  _helper.pre_iterate();
}

void G1EvacFailureObjectsSet::iterate(ObjectClosure* closure) const {
  assert_at_safepoint();
  _helper.iterate(closure);
}

void G1EvacFailureObjectsSet::post_iterate() {
  _helper.post_iterate();
  _offsets.drop_all();
}

int G1EvacFailureObjectsSet::G1EvacFailureObjectsIterationHelper::order_oop(OffsetInRegion a, OffsetInRegion b) {
  return static_cast<int>(a-b);
}

void G1EvacFailureObjectsSet::G1EvacFailureObjectsIterationHelper::join_and_sort() {
  _segments->iterate_nodes(*this);

  QuickSort::sort(_offset_array, _array_length, order_oop, true);
}

void G1EvacFailureObjectsSet::G1EvacFailureObjectsIterationHelper::iterate_internal(ObjectClosure* closure) const {
  for (uint i = 0; i < _array_length; i++) {
    oop cur = _objects_set->from_offset(_offset_array[i]);
    closure->do_object(cur);
  }
}

G1EvacFailureObjectsSet::G1EvacFailureObjectsIterationHelper::G1EvacFailureObjectsIterationHelper(G1EvacFailureObjectsSet* collector) :
  _objects_set(collector),
  _segments(&_objects_set->_offsets),
  _offset_array(nullptr),
  _array_length(0),
  _num_allocated_nodes(0) { }

void G1EvacFailureObjectsSet::G1EvacFailureObjectsIterationHelper::pre_iterate() {
  assert(_offset_array == nullptr, "must be");
  assert(_array_length == 0, "must be");
  _num_allocated_nodes = _segments->num_allocated_nodes();
  _offset_array = NEW_C_HEAP_ARRAY(OffsetInRegion, _num_allocated_nodes, mtGC);

  join_and_sort();
}

void G1EvacFailureObjectsSet::G1EvacFailureObjectsIterationHelper::iterate(ObjectClosure* closure) const {
  assert(_array_length == _num_allocated_nodes, "must be %u, %u",
         _array_length, _num_allocated_nodes);
  iterate_internal(closure);
}

void G1EvacFailureObjectsSet::G1EvacFailureObjectsIterationHelper::post_iterate() {
  assert(_offset_array != nullptr, "must be");
  assert(_array_length != 0, "must be");
  FREE_C_HEAP_ARRAY(OffsetInRegion, _offset_array);
  _offset_array = nullptr;
  _array_length = 0;
  _num_allocated_nodes = 0;
}

// Callback of G1SegmentedArray::iterate_nodes
void G1EvacFailureObjectsSet::G1EvacFailureObjectsIterationHelper::do_buffer(G1SegmentedArrayBuffer<mtGC>* node, uint length) {
  node->copy_to(&_offset_array[_array_length]);
  _array_length += length;
}
