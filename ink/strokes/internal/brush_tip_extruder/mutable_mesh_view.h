// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INK_STROKES_INTERNAL_BRUSH_TIP_EXTRUDER_MUTABLE_MESH_VIEW_H_
#define INK_STROKES_INTERNAL_BRUSH_TIP_EXTRUDER_MUTABLE_MESH_VIEW_H_

#include <array>
#include <cstdint>
#include <variant>
#include <vector>

#include "ink/geometry/mutable_mesh.h"
#include "ink/geometry/point.h"
#include "ink/geometry/triangle.h"
#include "ink/geometry/vec.h"
#include "ink/strokes/internal/brush_tip_extruder/extruded_vertex.h"
#include "ink/strokes/internal/legacy_vertex.h"
#include "ink/strokes/internal/stroke_vertex.h"

namespace ink::brush_tip_extruder_internal {

// An indexed triangle mesh that does not own the storage for its vertices and
// indices, and keeps track of mesh mutations.
//
// This allows code that builds and modifies triangle mesh data (vertices and
// triangle indices only) to work on various mesh-like types without requiring
// templates or dynamic dispatch. Here, "mesh-like" describes types that also
// store rendering properties such as texture or shader information, and uniform
// properties like a transformation matrix.
//
// The type keeps track of the first vertex and triangle in the mesh that has
// been modified through it. This is useful for efficiently syncing changes with
// GPU buffers when the expected changes to the mesh consist of appending or
// modifying values close to the end.
//
// The type does not perform validation that would require greater than
// constant-time checking or that specifies the order of appending vertices and
// triangle indices. E.g.
//   * Out-of-bounds checking is performed when returning the position at a
//     given index.
//   * No check is performed when shrinking the vertices of the mesh that all
//     triangle indices are still valid.
//   * No check is performed when appending triangle indices that each index is
//     valid at the time of appending.
//
// Note that the various "get" functions do not return references or spans. E.g.
// `GetVertex()` returns a `Vertex` by value and `GetTriangleIndices()` returns
// a `std::array` rather than an `absl::Span`. This is intentionally done to
// support both legacy and new vertex memory layouts.
class MutableMeshView {
 public:
  // The type used for vertex and triangle indices in a `MutableMeshView`.
  using IndexType = uint32_t;

  // Constructs a view with no mesh data.
  MutableMeshView() = default;

  // Constructs a view with a reference to a mutable `mesh`, which must outlive
  // this object.
  //
  // CHECK-fails if the unpacked representation of `mesh.Format()` is not
  // equivalent to that of `StrokeVertex::FullMeshFormat()`,
  explicit MutableMeshView(MutableMesh& mesh);

  // Constructs a view with references to `vertices` and `indices`.
  //
  // Like with a string_view, the passed-in vectors must outlive this object.
  MutableMeshView(std::vector<strokes_internal::LegacyVertex>& vertices,
                  std::vector<IndexType>& indices);

  MutableMeshView(const MutableMeshView&) = default;
  MutableMeshView& operator=(const MutableMeshView&) = default;

  // Removes all triangles and vertices from the underlying mesh data. If
  // `HasMeshData()` is false, this is a no-op.
  void Clear();

  // Returns true if the view has pointers to vertex and index data (i.e. it was
  // not default constructed).
  //
  // All members below are valid to call only if `HasMeshData()` would return
  // true.
  bool HasMeshData() const;

  uint32_t VertexCount() const;
  uint32_t TriangleCount() const;

  Point GetPosition(IndexType index) const;

  // The following return the side and forward derivatives, respectively.
  //
  // The return value will always be the zero-vector if the mesh view references
  // legacy vertices and indices rather than a `MutableMesh`.
  Vec GetSideDerivative(IndexType index) const;
  Vec GetForwardDerivative(IndexType index) const;

  // The following return the side and forward label, respectively.
  //
  // The return value will always be `StrokeVertex::kInteriorLabel` if the mesh
  // view references legacy vertices and indices rather than a `MutableMesh`.
  strokes_internal::StrokeVertex::Label GetSideLabel(IndexType index) const;
  strokes_internal::StrokeVertex::Label GetForwardLabel(IndexType index) const;

  ExtrudedVertex GetVertex(IndexType index) const;

  Triangle GetTriangle(IndexType triangle) const;
  std::array<IndexType, 3> GetTriangleIndices(IndexType triangle) const;
  IndexType GetVertexIndex(IndexType triangle, uint32_t triangle_vertex) const;

  // Appends a new vertex.
  //
  // NOTE: As with other members, this is an illegal operation when
  // `HasMeshData()` is false.
  void AppendVertex(const ExtrudedVertex& vertex);

  // Appends a new triplet of triangle indices.
  //
  // NOTE: As with other members, this is an Illegal operation when
  // `HasMeshData()` is false.
  void AppendTriangleIndices(const std::array<IndexType, 3>& indices);

  // The following set the value of the side and forward derivatives,
  // respectively.
  //
  // These functions are a no-op if the mesh view references legacy vertices and
  // indices rather than a `MutableMesh`.
  void SetSideDerivative(IndexType index, Vec derivative);
  void SetForwardDerivative(IndexType index, Vec derivative);

  // The following set the value of the side and forward labels, respectively.
  //
  // These functions are a no-op if the mesh view references legacy vertices and
  // indices rather than a `MutableMesh`.
  void SetSideLabel(IndexType index,
                    strokes_internal::StrokeVertex::Label label);
  void SetForwardLabel(IndexType index,
                       strokes_internal::StrokeVertex::Label label);

  void SetVertex(IndexType index, const ExtrudedVertex& vertex);

  void SetTriangleIndices(IndexType triangle,
                          const std::array<IndexType, 3>& indices);

  // Inserts a new triplet of indices.
  //
  // NOTE: As with other members, this is an Illegal operation when
  // `HasMeshData()` is false.
  void InsertTriangleIndices(IndexType triangle,
                             const std::array<IndexType, 3>& indices);

  // Removes triangles/vertices from the mesh, if the new count is smaller than
  // the old. If the new count is greater than or equal to the old, these have
  // no effect.
  void TruncateTriangles(uint32_t new_triangle_count);
  void TruncateVertices(uint32_t new_vertex_count);

  // Returns the index of the first new or updated vertex since construction or
  // the last call to `ResetMutationTracking()`.
  IndexType FirstMutatedVertex() const;

  // Returns the index of the first new or updated triangle since construction
  // or the last call to `ResetMutationTracking()`.
  IndexType FirstMutatedTriangle() const;

  // Resets mutation tracking so that all vertices and triangle indices
  // currently in the mesh are considered "not mutated" after this call.
  void ResetMutationTracking();

 private:
  struct LegacyVectors {
    std::vector<strokes_internal::LegacyVertex>* vertices;
    std::vector<IndexType>* indices;
  };

  std::variant<std::monostate, LegacyVectors, MutableMesh*> data_;
  IndexType first_mutated_triangle_ = 0;
  IndexType first_mutated_vertex_ = 0;
};

// ---------------------------------------------------------------------------
//                     Implementation details below

inline bool MutableMeshView::HasMeshData() const {
  return !std::holds_alternative<std::monostate>(data_);
}

inline MutableMeshView::IndexType MutableMeshView::FirstMutatedVertex() const {
  return first_mutated_vertex_;
}

inline MutableMeshView::IndexType MutableMeshView::FirstMutatedTriangle()
    const {
  return first_mutated_triangle_;
}

}  // namespace ink::brush_tip_extruder_internal

#endif  // INK_STROKES_INTERNAL_BRUSH_TIP_EXTRUDER_MUTABLE_MESH_VIEW_H_
