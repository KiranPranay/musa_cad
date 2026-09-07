// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "musacad/core/entity_handle.hpp"
#include "musacad/core/generational_arena.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/mtext_block.hpp"
#include "musacad/core/table_types.hpp"
#include "musacad/core/page_setup.hpp"
#include "musacad/core/properties.hpp"

namespace musacad::core {

class IFontEngine;
class IImageDecoder;

// ---------------------------------------------------------------------------
// Per-primitive SoA records. Fixed-size primitives store their data inline;
// variable-length primitives (polyline, spline) store an (offset, count) view
// into a shared contiguous vertex pool.
// ---------------------------------------------------------------------------

// Every primitive carries an EntityProps column (layer ref + ByLayer/override
// colour, linetype, lineweight). Defaults to layer 0, fully ByLayer.
struct PointData {
    Vec2 p;
    EntityProps props{};
};

struct LineData {
    Vec2 a;
    Vec2 b;
    EntityProps props{};
};

struct CircleData {
    Vec2 center;
    double radius;
    EntityProps props{};
};

/// Arc on a circle, swept counter-clockwise from start_angle to end_angle
/// (radians). end_angle may exceed start_angle; the kernel normalises the sweep.
struct ArcData {
    Vec2 center;
    double radius;
    double start_angle;
    double end_angle;
    EntityProps props{};
};

struct PolylineData {
    static constexpr std::uint32_t kNoBulges = 0xFFFFFFFFu; ///< all segments straight

    std::uint32_t offset; ///< first vertex index in the polyline vertex pool
    std::uint32_t count;  ///< number of vertices
    std::uint32_t bulge_offset = kNoBulges; ///< first of `count` bulges, or kNoBulges
    bool closed;
    EntityProps props{};
};

struct SplineData {
    std::uint32_t offset; ///< first control point in the spline pool
    std::uint32_t count;  ///< number of control points
    std::uint32_t degree;
    EntityProps props{};
};

/// Single-line text. The string lives in a shared char pool (offset, len) like
/// polyline vertices -- no fat inline buffer on the per-entity struct.
struct TextData {
    Vec2 pos;                  ///< insertion point (justification anchor, on the baseline)
    double height = 1.0;
    double rotation = 0.0;     ///< radians, CCW
    std::uint8_t justify = 0;  ///< 0 = left, 1 = center, 2 = right
    std::uint16_t font = 0;    ///< index into the store's font table (0 = Standard/stroke)
    std::uint32_t str_offset = 0;
    std::uint32_t str_len = 0;
    EntityProps props{};
};

/// A composite dimension. The measured value is COMPUTED from the definition
/// points (a, b) -- never baked -- so editing them updates the dimension. `line_pt`
/// positions the dimension line; `style` indexes the dimstyle table. DimType lives
/// in properties.hpp.
///
/// Text decoration (prefix / suffix / tolerance) qualifies the value without ever
/// replacing it: a dimension still cannot lie about the geometry. The two strings
/// live in the shared char pool as (offset, len) -- TextData's pattern, no fat inline
/// buffer -- and are expanded through substitute_text_codes at layout time, so
/// "%%c200 H7" and "6X" work with no dimension-specific code.
struct DimData {
    DimType type = DimType::Linear;
    Vec2 a;       ///< first definition point
    Vec2 b;       ///< second definition point
    Vec2 line_pt; ///< a point the dimension line passes through (placement)
    std::uint16_t style = 0;
    EntityProps props{};
    DimOverrides overrides{}; ///< per-dimension style overrides (ByStyle by default)
    DimTolerance tol{};       ///< tolerance mode + deviations (None by default)
    std::uint32_t prefix_offset = 0; ///< prefix string in the shared char pool
    std::uint32_t prefix_len = 0;
    std::uint32_t suffix_offset = 0; ///< suffix string in the shared char pool
    std::uint32_t suffix_len = 0;
    /// AutoCAD's text OVERRIDE (issue #20), in the shared char pool. Empty = derive the
    /// label from the measurement. `<>` inside it expands to the measured value, so
    /// "<> H7" tracks the geometry. The measurement is still computed either way -- an
    /// override can always be inspected and removed.
    std::uint32_t override_offset = 0;
    std::uint32_t override_len = 0;
    /// Displacement of the label from its DERIVED position (issue #21), in the text's
    /// own frame: x along the baseline, y along baseline->cap. (0,0) = derived, which is
    /// what every existing drawing loads as. Stored rather than baked, so the automatic
    /// ISO 129-1 fit (#12) still runs and the value stays measured.
    Vec2 text_offset{};
    /// One extra datum the newer types need (v23): the ordinate axis (0 = X, 1 = Y), a
    /// jogged dimension's jog position along its line (0..1), or an arc-length
    /// dimension's end angle in radians. Zero for the classic types.
    double aux = 0.0;

    /// Grip index of the label. Deliberately outside the contiguous def-point/foot range
    /// so adding grips to any dimension type can never collide with it.
    static constexpr std::uint32_t kTextGripIndex = 100;
};

/// A quick leader: an arrowhead at `tip`, a leader line to `knee`, and a text
/// label anchored at `knee`. Shares the dimstyle arrow + the stroke font.
struct LeaderData {
    Vec2 tip;              ///< arrowhead point (what the leader points at)
    Vec2 knee;            ///< landing / text anchor
    double text_height = 2.5;
    std::uint16_t style = 0; ///< dimstyle (for arrow type/size + colours)
    std::uint16_t font = 0;  ///< index into the store's font table (0 = Standard/stroke)
    std::uint32_t str_offset = 0;
    std::uint32_t str_len = 0;
    EntityProps props{};
    DimOverrides overrides{}; ///< per-leader arrow override (only the arrow bits used; ByStyle default)
};

/// Multi-line paragraph text (MTEXT): a formatting block + layer properties.
struct MTextData {
    MTextBlock text;
    EntityProps props{};
};

/// An editable leader (QLEADER): a leader polyline (vertices in the shared pool;
/// vertex 0 is the arrow tip, the last is the landing) drawn with a dimstyle
/// arrow, plus an OWNED paragraph-text label (the same MTextBlock + layout as
/// MTEXT -- no text fork). Ownership is the association: moving the leader moves
/// the text; there is no cross-entity reference to dangle.
struct MLeaderData {
    std::uint32_t vtx_offset = 0; ///< first leader vertex in the polyline pool
    std::uint32_t vtx_count = 0;  ///< number of leader vertices (>= 1)
    std::uint16_t style = 0;      ///< dimstyle (arrow type/size + colours)
    MTextBlock text;              ///< the attached label (computed layout)
    EntityProps props{};
    DimOverrides overrides{}; ///< per-leader arrow override (only the arrow bits used; ByStyle default)
};

/// A HATCH: a filled/patterned region bounded by one or more CLOSED loops (the first
/// loop is the outer boundary; the rest are islands/holes). Loop vertices live in the
/// shared hatch vertex pool (contiguous, all loops back-to-back); the per-loop vertex
/// counts live in a parallel pool (so a loop's vertices are a subspan). The pattern
/// is a NAME (in the string pool; "SOLID" = filled) plus scale/angle/origin. The fill
/// or pattern geometry is COMPUTED at snapshot time (derived-not-baked) -- the store
/// keeps only the boundary + pattern parameters.
struct HatchData {
    std::uint32_t vtx_offset = 0;   ///< first loop vertex in hatch_vtx_pool_
    std::uint32_t vtx_count = 0;    ///< total vertices across all loops
    std::uint32_t loop_offset = 0;  ///< first per-loop length in hatch_loop_lens_
    std::uint32_t loop_count = 0;   ///< number of loops (>= 1; loop 0 = outer)
    std::uint32_t str_offset = 0;   ///< pattern name in the string pool
    std::uint32_t str_len = 0;
    double pattern_scale = 1.0;
    double pattern_angle = 0.0; ///< radians, CCW
    Vec2 pattern_origin{};
    EntityProps props{};
};

/// One cell of a feature control frame. A cell is TEXT -- the characteristic symbol,
/// the tolerance value, or a datum reference -- held in the shared char pool exactly
/// like every other string in the store. The GD&T *meaning* comes from the cell's
/// position (cell 0 is the characteristic) and from the symbols the stroke font now
/// carries (issue #9), so a cell needs no per-cell semantic tag and no second alphabet.
struct FcfCell {
    std::uint32_t str_offset = 0;
    std::uint32_t str_len = 0;
    friend bool operator==(const FcfCell&, const FcfCell&) = default;
};

/// A feature control frame (GD&T). An ordered, VARIABLE-LENGTH cell list held as an
/// (offset, count) view into a shared cell pool -- the polyline/spline pattern -- plus
/// an insertion point, rotation, a dimstyle index and per-entity overrides.
///
/// Borders, dividers, glyphs and cell rectangles are DERIVED from the text height by
/// compute_fcf_geometry at snapshot time, never baked, so editing the dimstyle re-lays
/// every frame out. Sharing DIMSTYLE + DimOverrides with dimensions and leaders is the
/// point of the issue: GD&T annotation matches the drawing's dimensions automatically.
struct FcfData {
    std::uint32_t cell_offset = 0; ///< first cell in the shared FCF cell pool
    std::uint32_t cell_count = 0;  ///< number of cells (>= 1)
    Vec2 pos;                      ///< insertion point: left edge, on the frame's baseline
    double rotation = 0.0;         ///< radians, CCW
    std::uint16_t style = 0;       ///< dimstyle (text height, colours, lineweight)
    EntityProps props{};
    DimOverrides overrides{}; ///< per-frame overrides (the same machinery dimensions use)
};

/// A datum feature symbol: the boxed datum letter, a leader from the box to the
/// feature, and the FILLED triangle that touches it. `tip` is the triangle point;
/// `pos` anchors the box. Like the frame, all of its geometry is derived from the
/// text height at snapshot time.
/// A construction line (AutoCAD XLINE / RAY). Stored as a base point and a UNIT
/// direction; `ray` makes it semi-infinite (extends only in +dir). It has no finite
/// extent, so it is excluded from the spatial index and from ZOOM Extents, and the
/// renderer clips it to the viewport each frame -- exactly AutoCAD's treatment.
struct XlineData {
    Vec2 base;
    Vec2 dir{1.0, 0.0}; ///< unit direction
    bool ray = false;   ///< false = XLINE (both ways), true = RAY (+dir only)
    EntityProps props{};
};

/// An ellipse or elliptical arc (AutoCAD ELLIPSE). Centre, MAJOR half-axis vector (its
/// length is the major radius, its direction the rotation), minor/major RATIO in (0,1],
/// and a counter-clockwise parameter range (full = 0..2pi). Everything drawn is derived
/// from these through core/ellipse.hpp; the DXF ELLIPSE entity has the same fields.
struct EllipseData {
    Vec2 center;
    Vec2 major{1.0, 0.0};
    double ratio = 1.0;
    double start = 0.0;
    double end = kTwoPi;
    EntityProps props{};
};

struct DatumData {
    Vec2 tip;                     ///< the point on the feature (triangle apex)
    Vec2 pos;                     ///< box anchor (left edge, on the box's baseline)
    double rotation = 0.0;        ///< radians, CCW (rotates the box, not the leader)
    std::uint16_t style = 0;      ///< dimstyle
    std::uint32_t str_offset = 0; ///< the datum letter(s), in the shared char pool
    std::uint32_t str_len = 0;
    EntityProps props{};
    DimOverrides overrides{};
};

/// A TABLE: a grid of cells with stored column widths and row heights, plus an insertion
/// point, rotation and a table-style index. Borders, grid lines, cell rectangles and text
/// placement are all DERIVED by compute_table_geometry -- never baked -- so a style edit
/// re-lays out every table on the next snapshot.
///
/// The insertion point is the table's TOP-left corner, because that is the corner a
/// drafter places and the direction rows grow from (AutoCAD's behaviour).
struct TableData {
    std::uint32_t cell_offset = 0; ///< first cell in the shared table-cell pool
    std::uint32_t size_offset = 0; ///< first entry in the shared size pool:
                                   ///< `cols` widths followed by `rows` heights
    std::uint16_t rows = 0;
    std::uint16_t cols = 0;
    bool has_title = false;  ///< row 0 is a title row (title text height, merged across)
    bool has_header = false; ///< the row after any title is a header row
    Vec2 pos;                ///< insertion point: the table's TOP-left corner
    double rotation = 0.0;   ///< radians, CCW
    std::uint16_t style = 0; ///< index into the table-style table
    EntityProps props{};
};

/// A raster image DEFINITION, held in a table on the store parallel to the layer /
/// dimstyle / block-definition tables. N placements of one logo are N small entities
/// plus ONE definition -- the BLOCKDEF/INSERT shape -- which gives dedup and one place
/// for the payload.
///
/// The payload is either an external `source` path resolved RELATIVE TO THE DRAWING, or
/// base64 bytes embedded in the file so a .musa stays self-contained. Both are supported
/// because they trade off differently for version control and for sharing.
///
/// `version` bumps whenever the payload changes, so a renderer-side texture cache keyed
/// by definition index knows when to re-upload. Decoded pixels are NOT stored here.
struct ImageDef {
    std::string source;              ///< external path (relative to the drawing) or ""
    std::vector<std::uint8_t> bytes; ///< embedded ENCODED bytes (PNG/JPEG), or empty
    std::uint32_t pixel_w = 0;       ///< as decoded; 0 until the decoder has run
    std::uint32_t pixel_h = 0;
    std::uint32_t version = 1;       ///< bumped on payload change (texture-cache key)
    friend bool operator==(const ImageDef&, const ImageDef&) = default;
};

/// A placed raster image: a definition index plus a transform and an optional clip.
/// Its quad is DERIVED from these by resolve_image_quad -- never baked -- so the same
/// rule the rest of the model follows applies here too.
///
/// `clip_*` are FRACTIONS of the image (0..1, u right, v down from the top-left), so a
/// clip survives the image being resized or its definition being re-pointed at a
/// different-resolution file.
struct ImageData {
    std::uint16_t def = 0;   ///< index into the image-definition table
    Vec2 pos;                ///< insertion point (bottom-left corner of the quad)
    double width = 1.0;      ///< drawing units
    double height = 1.0;     ///< drawing units
    double rotation = 0.0;   ///< radians, CCW about `pos`
    bool clipped = false;
    double clip_u0 = 0.0;    ///< clip rectangle in image fractions
    double clip_v0 = 0.0;
    double clip_u1 = 1.0;
    double clip_v1 = 1.0;
    EntityProps props{};
};

// ---------------------------------------------------------------------------
// Blocks. A block DEFINITION is a named, self-contained collection of geometry
// (kept in the block-definition table, parallel to the layer table -- NOT in the
// model-space arenas, so it never appears in snapshot/pick/all_live on its own).
// A model-space INSERT references a definition by index and carries a transform;
// its geometry is resolved (definition x transform) at snapshot, never baked.
// ---------------------------------------------------------------------------

/// A model-space block reference (its own arena). `block` indexes the block table.
struct InsertData {
    std::uint16_t block = 0; ///< index into the block-definition table
    Vec2 pos;                ///< insertion point (world)
    double scale_x = 1.0;
    double scale_y = 1.0;
    double rotation = 0.0; ///< radians, CCW
    EntityProps props{};   ///< the insert's own layer/colour (ByBlock source for members)
};

// Self-contained block-content primitives (pool-free, unlike the model-space
// records that index shared pools). LineData/CircleData/ArcData are already
// self-contained, so they are reused directly.
struct BlockPolyline {
    std::vector<Vec2> verts;
    std::vector<double> bulges; ///< empty (all straight) or same length as verts
    bool closed = false;
    EntityProps props{};
};
struct BlockText {
    Vec2 pos;
    double height = 1.0;
    double rotation = 0.0;
    std::uint8_t justify = 0;
    std::string content;
    EntityProps props{};
};
struct BlockMText {
    MTextBlock block; ///< str_offset/str_len ignored; content is inline
    std::string content;
    EntityProps props{};
};

/// The geometry of a block definition. Inserts may nest (a block placing other
/// blocks); resolution composes transforms with a depth guard.
struct BlockContent {
    std::vector<LineData> lines;
    std::vector<CircleData> circles;
    std::vector<ArcData> arcs;
    std::vector<BlockPolyline> polylines;
    std::vector<BlockText> texts;
    std::vector<BlockMText> mtexts;
    std::vector<InsertData> inserts; ///< nested block references
};

/// A named block definition + its base point (the local origin INSERTs align to).
struct BlockDef {
    std::string name;
    Vec2 base{0.0, 0.0};
    BlockContent content;
};

/// Structure-of-Arrays geometry storage. Each primitive kind lives in its own
/// GenerationalArena; variable-length vertex data lives in shared pools. All
/// access is non-virtual.
///
/// Note (Phase 2): removing a polyline/spline frees its arena slot but leaves
/// its vertices in the pool, so other handles' (offset, count) views stay
/// valid. Pool compaction is a future optimisation.
class GeometryStore {
public:
    // --- creation (props default to layer 0, fully ByLayer) -----------------
    EntityHandle add_point(Vec2 p, EntityProps props = {});
    /// A construction line through `base` along unit `dir`; `ray` = semi-infinite.
    EntityHandle add_xline(Vec2 base, Vec2 dir, bool ray, EntityProps props = {});
    /// An ellipse / elliptical arc; ratio is clamped to (0,1], a full range is 0..2pi.
    EntityHandle add_ellipse(Vec2 center, Vec2 major, double ratio, double start, double end,
                             EntityProps props = {});
    EntityHandle add_line(Vec2 a, Vec2 b, EntityProps props = {});
    EntityHandle add_circle(Vec2 center, double radius, EntityProps props = {});
    EntityHandle add_arc(Vec2 center, double radius, double start_angle, double end_angle,
                         EntityProps props = {});
    EntityHandle add_polyline(std::span<const Vec2> vertices, bool closed, EntityProps props = {});
    /// Polyline with per-vertex arc bulges (b = tan(theta/4); 0 = straight). `bulges`
    /// must be empty (all straight) or the same length as `vertices`.
    EntityHandle add_polyline(std::span<const Vec2> vertices, std::span<const double> bulges,
                              bool closed, EntityProps props = {});
    EntityHandle add_spline(std::span<const Vec2> control_points, std::uint32_t degree,
                            EntityProps props = {});
    EntityHandle add_text(Vec2 pos, double height, double rotation, std::uint8_t justify,
                          std::string_view content, EntityProps props = {}, std::uint16_t font = 0);
    /// `prefix`/`suffix` are copied into the shared char pool; they and `tol` decorate
    /// the MEASURED value, never replace it.
    /// Sets the extra datum of a dimension (see DimData::aux); false if `h` is not one.
    bool set_dim_aux(EntityHandle h, double aux) noexcept;
    EntityHandle add_dimension(DimType type, Vec2 a, Vec2 b, Vec2 line_pt, std::uint16_t style,
                               EntityProps props = {}, DimOverrides overrides = {},
                               std::string_view prefix = {}, std::string_view suffix = {},
                               DimTolerance tol = {}, std::string_view text_override = {},
                               Vec2 text_offset = {});
    EntityHandle add_leader(Vec2 tip, Vec2 knee, double text_height, std::uint16_t style,
                            std::string_view content, EntityProps props = {},
                            std::uint16_t font = 0, DimOverrides overrides = {});
    /// Multi-line paragraph text. `block.str_offset/str_len` are ignored; `content`
    /// is copied into the shared string pool and the range is recorded.
    EntityHandle add_mtext(const MTextBlock& block, std::string_view content,
                           EntityProps props = {});
    /// Editable leader with an owned paragraph label. `vertices[0]` is the arrow tip.
    EntityHandle add_mleader(std::span<const Vec2> vertices, std::uint16_t style,
                             const MTextBlock& text, std::string_view content,
                             EntityProps props = {}, DimOverrides overrides = {});
    /// Create a HATCH from closed boundary loops (loop 0 = outer, the rest islands),
    /// a pattern name ("SOLID" = filled), and pattern scale/angle(radians)/origin.
    EntityHandle add_hatch(const std::vector<std::vector<Vec2>>& loops, std::string_view pattern,
                           double scale, double angle, Vec2 origin, EntityProps props = {});
    /// A feature control frame. `cells` are the raw cell strings in order (cell 0 is the
    /// characteristic symbol); they are copied into the shared char pool.
    EntityHandle add_fcf(const std::vector<std::string>& cells, Vec2 pos, double rotation,
                         std::uint16_t style, EntityProps props = {}, DimOverrides overrides = {});
    /// A datum feature symbol (boxed letter + leader + filled triangle).
    EntityHandle add_datum(std::string_view letter, Vec2 tip, Vec2 pos, double rotation,
                           std::uint16_t style, EntityProps props = {},
                           DimOverrides overrides = {});
    /// A placed raster image referencing `def` in the image-definition table.
    EntityHandle add_image(std::uint16_t def, Vec2 pos, double width, double height,
                           double rotation, EntityProps props = {});
    /// A table. `cells` are the raw cell strings in ROW-MAJOR order (`rows * cols` of
    /// them); `col_widths` and `row_heights` size the grid.
    EntityHandle add_table(std::uint16_t rows, std::uint16_t cols,
                           const std::vector<TableCell>& cells,
                           const std::vector<double>& col_widths,
                           const std::vector<double>& row_heights, Vec2 pos, double rotation,
                           std::uint16_t style, bool has_title = false, bool has_header = false,
                           EntityProps props = {});
    /// A model-space block reference into the block-definition table.
    EntityHandle add_insert(std::uint16_t block, Vec2 pos, double scale_x, double scale_y,
                            double rotation, EntityProps props = {});

    // --- removal / validity -------------------------------------------------
    bool remove(EntityHandle handle) noexcept;
    [[nodiscard]] bool is_valid(EntityHandle handle) const noexcept;
    [[nodiscard]] std::size_t live_count() const noexcept;

    /// Drops every entity and vertex pool, leaving an empty store (used by
    /// New / Open). Generations are not preserved -- handles are runtime-only.
    void clear() noexcept;

    // --- typed accessors (nullptr if invalid or wrong kind) -----------------
    [[nodiscard]] const PointData* point(EntityHandle h) const noexcept;
    [[nodiscard]] const XlineData* xline(EntityHandle h) const noexcept;
    [[nodiscard]] const EllipseData* ellipse(EntityHandle h) const noexcept;
    [[nodiscard]] const LineData* line(EntityHandle h) const noexcept;
    [[nodiscard]] const CircleData* circle(EntityHandle h) const noexcept;
    [[nodiscard]] const ArcData* arc(EntityHandle h) const noexcept;
    [[nodiscard]] const PolylineData* polyline(EntityHandle h) const noexcept;
    [[nodiscard]] const SplineData* spline(EntityHandle h) const noexcept;
    [[nodiscard]] const TextData* text(EntityHandle h) const noexcept;
    [[nodiscard]] const DimData* dimension(EntityHandle h) const noexcept;
    [[nodiscard]] const LeaderData* leader(EntityHandle h) const noexcept;
    [[nodiscard]] const MTextData* mtext(EntityHandle h) const noexcept;
    [[nodiscard]] const MLeaderData* mleader(EntityHandle h) const noexcept;
    [[nodiscard]] const InsertData* insert(EntityHandle h) const noexcept;
    [[nodiscard]] const HatchData* hatch(EntityHandle h) const noexcept;
    [[nodiscard]] const FcfData* fcf(EntityHandle h) const noexcept;
    [[nodiscard]] const DatumData* datum(EntityHandle h) const noexcept;
    [[nodiscard]] const ImageData* image(EntityHandle h) const noexcept;
    [[nodiscard]] const TableData* table(EntityHandle h) const noexcept;
    /// Mutable access for the create path only (the clip fields are set right after
    /// insertion); everything else reads through the const accessor.
    [[nodiscard]] ImageData* mutable_image(EntityHandle h) noexcept;
    /// The string content of a text entity.
    [[nodiscard]] std::string_view string_of(const TextData& t) const noexcept;
    [[nodiscard]] std::string_view string_of(const LeaderData& l) const noexcept;
    /// A dimension's raw (unexpanded) prefix / suffix strings.
    [[nodiscard]] std::string_view dim_prefix(const DimData& d) const noexcept;
    [[nodiscard]] std::string_view dim_suffix(const DimData& d) const noexcept;
    /// Both at once, in the shape compute_dim_geometry takes. Every consumer of a
    /// dimension's geometry goes through this, so none of them can render an
    /// undecorated label by forgetting a parameter.
    [[nodiscard]] std::string_view dim_override(const DimData& d) const noexcept {
        return {string_pool_.data() + d.override_offset, d.override_len};
    }
    [[nodiscard]] DimTextParts dim_text_parts(const DimData& d) const noexcept {
        return DimTextParts{dim_prefix(d), dim_suffix(d), dim_override(d)};
    }
    /// Content of a paragraph-text block (MTEXT entity or QLEADER label).
    [[nodiscard]] std::string_view string_of(const MTextBlock& b) const noexcept;
    /// Leader-polyline vertices of an MLeader.
    [[nodiscard]] std::span<const Vec2> vertices_of(const MLeaderData& m) const noexcept;
    /// A hatch's pattern name (in the string pool; "SOLID" = filled).
    [[nodiscard]] std::string_view string_of(const HatchData& h) const noexcept;
    [[nodiscard]] std::string_view string_of(const FcfCell& c) const noexcept;
    [[nodiscard]] std::string_view string_of(const DatumData& d) const noexcept;
    /// The frame's cells as a contiguous span into the shared cell pool.
    [[nodiscard]] std::span<const FcfCell> fcf_cells(const FcfData& f) const noexcept;
    /// The frame's cell TEXT, code-substitution deferred to compute_fcf_geometry (the
    /// store keeps the raw strings -- derived-not-baked, like every other text).
    [[nodiscard]] std::vector<std::string_view> fcf_cell_text(const FcfData& f) const;
    /// A hatch's boundary loops, reconstructed (loop 0 = outer, the rest islands).
    [[nodiscard]] std::vector<std::vector<Vec2>> hatch_loops(const HatchData& h) const;
    /// All of a hatch's loop vertices, contiguous (all loops back-to-back) -- for bounds.
    [[nodiscard]] std::span<const Vec2> hatch_verts(const HatchData& h) const noexcept;

    // --- batch arena access (const; includes dead slots) --------------------
    [[nodiscard]] const GenerationalArena<PointData>& points() const noexcept { return points_; }
    [[nodiscard]] const GenerationalArena<XlineData>& xlines() const noexcept { return xlines_; }
    [[nodiscard]] const GenerationalArena<EllipseData>& ellipses() const noexcept {
        return ellipses_;
    }
    [[nodiscard]] const GenerationalArena<LineData>& lines() const noexcept { return lines_; }
    [[nodiscard]] const GenerationalArena<CircleData>& circles() const noexcept { return circles_; }
    [[nodiscard]] const GenerationalArena<ArcData>& arcs() const noexcept { return arcs_; }
    [[nodiscard]] const GenerationalArena<PolylineData>& polylines() const noexcept {
        return polylines_;
    }
    [[nodiscard]] const GenerationalArena<SplineData>& splines() const noexcept { return splines_; }
    [[nodiscard]] const GenerationalArena<TextData>& texts() const noexcept { return texts_; }
    [[nodiscard]] const GenerationalArena<DimData>& dimensions() const noexcept { return dims_; }
    [[nodiscard]] const GenerationalArena<LeaderData>& leaders() const noexcept { return leaders_; }
    [[nodiscard]] const GenerationalArena<MTextData>& mtexts() const noexcept { return mtexts_; }
    [[nodiscard]] const GenerationalArena<MLeaderData>& mleaders() const noexcept {
        return mleaders_;
    }
    [[nodiscard]] const GenerationalArena<InsertData>& inserts() const noexcept { return inserts_; }
    [[nodiscard]] const GenerationalArena<HatchData>& hatches() const noexcept { return hatches_; }
    [[nodiscard]] const GenerationalArena<FcfData>& fcfs() const noexcept { return fcfs_; }
    [[nodiscard]] const GenerationalArena<DatumData>& datums() const noexcept { return datums_; }
    [[nodiscard]] const GenerationalArena<ImageData>& images() const noexcept { return images_; }
    [[nodiscard]] const GenerationalArena<TableData>& tables() const noexcept { return tables_; }

    // --- block-definition table (parallel to the layer table) ---------------
    // Definitions are referenced by INSERTs by index. Few in number; a vector is
    // plenty. Indices are stable for a session.
    [[nodiscard]] const std::vector<BlockDef>& blocks() const noexcept { return blocks_; }
    [[nodiscard]] std::size_t block_count() const noexcept { return blocks_.size(); }
    [[nodiscard]] const BlockDef* block(std::uint16_t index) const noexcept;
    /// Adds a block definition, or returns the existing index if the name is taken.
    std::uint16_t add_block(const BlockDef& def);
    /// Replaces the block table (Open/Import).
    void set_block_table(std::vector<BlockDef> blocks);

    // --- font table (index 0 = "" = the built-in stroke font "Standard") -----
    // Few in number (the distinct font names a drawing references); a vector is
    // plenty. A text entity's `font` field indexes here; the snapshot resolves the
    // name through the injected IFontEngine (stroke vs TTF).
    [[nodiscard]] const std::vector<std::string>& fonts() const noexcept { return fonts_; }
    [[nodiscard]] std::string_view font_name(std::uint16_t index) const noexcept {
        return index < fonts_.size() ? std::string_view(fonts_[index]) : std::string_view();
    }
    /// Adds a font name (or returns the existing index). Empty name maps to 0 (Standard).
    std::uint16_t add_font(std::string_view name);
    /// Replaces the font table (Open/Import); ensures index 0 is the stroke font ("").
    void set_font_table(std::vector<std::string> fonts);
    /// The injected font engine that resolves outline-font names to glyph geometry +
    /// metrics. Non-owning; set by the GeometryEngine. Null = stroke font only. The same
    /// pointer feeds render/bounds/pick/grips so text geometry never forks.
    void set_font_engine(const IFontEngine* engine) noexcept { font_engine_ = engine; }
    [[nodiscard]] const IFontEngine* font_engine() const noexcept { return font_engine_; }

    // --- dimension styles ("Standard" always at index 0) --------------------
    [[nodiscard]] const std::vector<DimStyle>& dimstyles() const noexcept { return dimstyles_; }
    [[nodiscard]] const DimStyle* dimstyle(std::uint16_t index) const noexcept;
    std::uint16_t add_dimstyle(const DimStyle& style);
    bool set_dimstyle(std::uint16_t index, const DimStyle& style);
    /// Replaces the dimstyle table (Open/Import); ensures "Standard" at index 0.
    void set_dimstyle_table(std::vector<DimStyle> styles);

    // --- image-definition table (parallel to the layer / dimstyle tables) ---
    [[nodiscard]] const std::vector<ImageDef>& image_defs() const noexcept { return image_defs_; }
    [[nodiscard]] const ImageDef* image_def(std::uint16_t i) const noexcept {
        return i < image_defs_.size() ? &image_defs_[i] : nullptr;
    }
    /// Get-or-add by source path (the add_layer/add_dimstyle/add_block shape), so a
    /// drawing that places one logo ten times holds one definition.
    std::uint16_t add_image_def(const ImageDef& def);
    void set_image_def_table(std::vector<ImageDef> defs);

    // --- table-style table (parallel to the layer / dimstyle tables) ---
    [[nodiscard]] const std::vector<TableStyle>& table_styles() const noexcept {
        return table_styles_;
    }
    [[nodiscard]] const TableStyle* table_style(std::uint16_t i) const noexcept {
        return i < table_styles_.size() ? &table_styles_[i] : nullptr;
    }
    std::uint16_t add_table_style(const TableStyle& s);
    void set_table_style_table(std::vector<TableStyle> styles);

    /// A table's cells / column widths / row heights, as spans into the shared pools.
    [[nodiscard]] std::span<const TableCell> table_cells(const TableData& t) const noexcept;
    [[nodiscard]] std::span<const double> table_col_widths(const TableData& t) const noexcept;
    [[nodiscard]] std::span<const double> table_row_heights(const TableData& t) const noexcept;
    [[nodiscard]] std::string_view string_of(const TableCell& c) const noexcept;
    /// Copy `s` into the shared char pool and return its offset. Used by the create path
    /// for pooled-string entities whose command carries the text inline.
    [[nodiscard]] std::uint32_t intern_string(std::string_view s);
    /// The cells in the shape compute_table_geometry takes, so no consumer can build a
    /// table's geometry without its text.
    [[nodiscard]] std::vector<TableCellView> table_cell_views(const TableData& t) const;

    /// The injected raster decoder (IImageDecoder), or null. Null is not an error: the
    /// image's QUAD still resolves, so bounds, pick and grips work headlessly with no
    /// decoder at all -- only the pixels are unavailable.
    void set_image_decoder(const IImageDecoder* d) noexcept { image_decoder_ = d; }
    [[nodiscard]] const IImageDecoder* image_decoder() const noexcept { return image_decoder_; }

    // --- vertex pools -------------------------------------------------------
    [[nodiscard]] std::span<const Vec2> polyline_vertices() const noexcept {
        return polyline_pool_;
    }
    [[nodiscard]] std::span<const Vec2> spline_control_pool() const noexcept {
        return spline_pool_;
    }
    /// The vertex view for a given polyline/spline record.
    [[nodiscard]] std::span<const Vec2> vertices_of(const PolylineData& pl) const noexcept;
    /// Per-vertex bulges for a polyline, or an empty span when all segments are
    /// straight. Same length as vertices_of() when non-empty.
    [[nodiscard]] std::span<const double> bulges_of(const PolylineData& pl) const noexcept;
    [[nodiscard]] std::span<const Vec2> control_points_of(const SplineData& sp) const noexcept;

    void reserve_lines(std::size_t n) { lines_.reserve(n); }

    // --- per-entity properties ----------------------------------------------
    /// The entity's property attributes (nullptr if invalid). Read-only.
    [[nodiscard]] const EntityProps* props(EntityHandle h) const noexcept;
    /// Replaces an entity's property attributes. Returns false if invalid.
    bool set_props(EntityHandle h, const EntityProps& props) noexcept;

    // --- layer table --------------------------------------------------------
    // Layer 0 always exists at index 0. Layers are few; a contiguous vector is
    // plenty. Indices are stable for a session's lifetime (no mid-session
    // removal reindex beyond the removed slot -- see remove_layer).
    [[nodiscard]] const std::vector<Layer>& layers() const noexcept { return layers_; }
    [[nodiscard]] std::size_t layer_count() const noexcept { return layers_.size(); }
    [[nodiscard]] const Layer* layer(std::uint16_t index) const noexcept;
    [[nodiscard]] std::uint16_t current_layer() const noexcept { return current_layer_; }
    void set_current_layer(std::uint16_t index) noexcept;

    /// Global linetype scale (AutoCAD LTSCALE): multiplies every dash pattern. Dash
    /// geometry is derived at snapshot from this + the stored linetype (not baked).
    [[nodiscard]] double ltscale() const noexcept { return ltscale_; }
    void set_ltscale(double scale) noexcept { ltscale_ = scale > 0.0 ? scale : ltscale_; }

    /// Per-entity linetype scale (AutoCAD CELTSCALE, DXF code 48); default 1.0. SPARSE:
    /// only entities with a non-default scale have an entry, so the hot data structs stay
    /// unfattened. The effective dash scale is LTSCALE * CELTSCALE (applied at snapshot).
    [[nodiscard]] double celtscale(EntityHandle h) const noexcept {
        const auto it = celtscale_.find(celtscale_key(h));
        return it != celtscale_.end() ? it->second : 1.0;
    }
    void set_celtscale(EntityHandle h, double scale) noexcept {
        const std::uint64_t k = celtscale_key(h);
        if (scale > 0.0 && scale != 1.0) {
            celtscale_[k] = scale;
        } else {
            celtscale_.erase(k); // 1.0 (or invalid) is the default -> no entry
        }
    }

    /// Saved PLOT page setups (persisted in the native format). Read-only for plotting;
    /// mutated only via add_page_setup / set_page_setups (Open/Import + the Save action).
    [[nodiscard]] const std::vector<PageSetup>& page_setups() const noexcept { return page_setups_; }
    void set_page_setups(std::vector<PageSetup> setups) { page_setups_ = std::move(setups); }
    /// Adds a setup, replacing any existing one with the same name (names are unique).
    void add_page_setup(const PageSetup& setup) {
        for (PageSetup& p : page_setups_) {
            if (p.name == setup.name) {
                p = setup;
                return;
            }
        }
        page_setups_.push_back(setup);
    }

    /// Adds a layer, or returns the existing index if the name is already taken
    /// (layer names are unique, AutoCAD-style).
    std::uint16_t add_layer(const Layer& layer);
    /// Replaces the entire layer table (used by Open/Import). Ensures at least
    /// layer 0 exists and clamps the current index.
    void set_layer_table(std::vector<Layer> layers, std::uint16_t current);
    /// Updates the layer at `index` (keeps the name unique; ignores a rename of
    /// layer 0). Returns false if the index is invalid.
    bool set_layer(std::uint16_t index, const Layer& layer);
    /// True if any live entity references the layer.
    [[nodiscard]] bool layer_in_use(std::uint16_t index) const noexcept;
    /// Removes a layer. Fails (returns false) for layer 0, the current layer, or
    /// a layer that still contains entities (AutoCAD rule). Remaining entities'
    /// layer indices above the removed one are shifted down to stay valid.
    bool remove_layer(std::uint16_t index);

private:
    void shift_layer_refs_after_removal(std::uint16_t removed) noexcept;

    /// Sparse-map key for CELTSCALE: (kind, slot). Stable while the entity lives; the
    /// entry is cleared on remove(), so a reused slot starts at the 1.0 default.
    [[nodiscard]] static std::uint64_t celtscale_key(EntityHandle h) noexcept {
        return (static_cast<std::uint64_t>(h.kind) << 32) | h.index;
    }

    GenerationalArena<PointData> points_;
    GenerationalArena<XlineData> xlines_;
    GenerationalArena<EllipseData> ellipses_;
    GenerationalArena<LineData> lines_;
    GenerationalArena<CircleData> circles_;
    GenerationalArena<ArcData> arcs_;
    GenerationalArena<PolylineData> polylines_;
    GenerationalArena<SplineData> splines_;
    GenerationalArena<TextData> texts_;
    GenerationalArena<DimData> dims_;
    GenerationalArena<LeaderData> leaders_;
    GenerationalArena<MTextData> mtexts_;
    GenerationalArena<MLeaderData> mleaders_;
    GenerationalArena<InsertData> inserts_;
    GenerationalArena<HatchData> hatches_;
    GenerationalArena<FcfData> fcfs_;
    GenerationalArena<DatumData> datums_;
    GenerationalArena<ImageData> images_;
    GenerationalArena<TableData> tables_;
    std::vector<TableCell> table_cell_pool_;  ///< shared, like the polyline vertex pool
    std::vector<double> table_size_pool_;     ///< column widths then row heights
    std::vector<TableStyle> table_styles_{TableStyle{"Standard"}}; // index 0 always present
    std::vector<ImageDef> image_defs_;                     // parallel to the layer table
    const IImageDecoder* image_decoder_ = nullptr;         // non-owning; injected service
    std::vector<FcfCell> fcf_cell_pool_; ///< shared, like the polyline vertex pool

    std::vector<Vec2> polyline_pool_;
    std::vector<double> bulge_pool_; // per-vertex polyline arc bulges
    std::vector<Vec2> spline_pool_;
    std::vector<char> string_pool_; // text content
    std::vector<Vec2> hatch_vtx_pool_;            // hatch boundary-loop vertices (all loops)
    std::vector<std::uint32_t> hatch_loop_lens_;  // per-loop vertex counts

    std::vector<Layer> layers_{Layer{"0"}}; // layer 0 always present
    std::uint16_t current_layer_ = 0;
    std::vector<DimStyle> dimstyles_{DimStyle{"Standard"}}; // index 0 always present
    double ltscale_ = 1.0;                                  // global linetype scale
    std::unordered_map<std::uint64_t, double> celtscale_;  // sparse per-entity CELTSCALE (def 1.0)
    std::vector<PageSetup> page_setups_;                    // saved PLOT page setups
    std::vector<BlockDef> blocks_;                          // block-definition table
    std::vector<std::string> fonts_{std::string{}};        // font table; [0] = stroke "Standard"
    const IFontEngine* font_engine_ = nullptr;             // non-owning; injected service
};

} // namespace musacad::core
