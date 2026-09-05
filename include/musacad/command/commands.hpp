// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <vector>

#include "musacad/command/command.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/properties.hpp"

namespace musacad::command {

// Each command is a small state machine. They share no control flow with the
// alias table or the processor.

class LineCommand final : public ICommand {
public:
    std::string name() const override { return "LINE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::vector<core::Vec2> points_;
    bool done_ = false;
};

class CircleCommand final : public ICommand {
public:
    std::string name() const override { return "CIRCLE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Center, Radius, Diameter } state_ = State::Center;
    core::Vec2 center_{};
    bool done_ = false;
};

class PolylineCommand final : public ICommand {
public:
    std::string name() const override { return "PLINE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    void prompt_next(CommandContext& ctx);
    std::vector<core::Vec2> points_;
    bool done_ = false;
};

class ArcCommand final : public ICommand {
public:
    std::string name() const override { return "ARC"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::vector<core::Vec2> points_;
    bool done_ = false;
};

class RectangleCommand final : public ICommand {
public:
    std::string name() const override { return "RECTANGLE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    // AwaitCorner is the AutoCAD "Specify other corner or [Area/Dimensions/Rotation]"
    // hub: it accepts the placement pick OR the option keywords. The Dim*/Area*/Rot
    // states gather typed values, exactly like CIRCLE's [Diameter] sub-step.
    enum class State {
        First,
        AwaitCorner,
        DimLen,
        DimWid,
        AreaVal,
        AreaSide,
        AreaSideVal,
        RotVal,
    } state_ = State::First;
    core::Vec2 first_{};
    double length_ = 0.0;   ///< fixed width along X (0 => corner-to-corner, no fixed size)
    double width_ = 0.0;    ///< fixed width along Y
    double rotation_ = 0.0; ///< radians, applied about first_
    double area_ = 0.0;
    bool area_by_length_ = true; ///< Area option: user gave Length (else Width)
    bool has_dims_ = false;      ///< fixed (length_, width_) chosen -> quadrant-flip placement
    bool done_ = false;
};

class EraseCommand final : public ICommand {
public:
    std::string name() const override { return "ERASE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool wants_selection() const override { return true; }

private:
    bool done_ = false;
};

class UndoCommand final : public ICommand {
public:
    std::string name() const override { return "U"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class ZoomCommand final : public ICommand {
public:
    std::string name() const override { return "ZOOM"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

// --- Modify commands (operate on the current selection / a pick) ---

class MoveCommand final : public ICommand {
public:
    std::string name() const override { return "MOVE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::optional<core::Vec2> base_;
    bool done_ = false;
};

class CopyCommand final : public ICommand {
public:
    std::string name() const override { return "COPY"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::optional<core::Vec2> base_;
    bool done_ = false;
};

class MirrorCommand final : public ICommand {
public:
    std::string name() const override { return "MIRROR"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { First, Second, Ask } state_ = State::First;
    core::Vec2 p1_{};
    core::Vec2 p2_{};
    bool done_ = false;
};

class OffsetCommand final : public ICommand {
public:
    std::string name() const override { return "OFFSET"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Distance, Object, Side } state_ = State::Distance;
    double distance_ = 0.0;
    core::Vec2 object_pick_{};
    bool done_ = false;
};

// JOIN: pick a source object, then pick lines/arcs/open polylines that share endpoints
// with it; commits a single polyline (closed if the chain loops). Pick-based like OFFSET.
class JoinCommand final : public ICommand {
public:
    std::string name() const override { return "JOIN"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Source, Targets } state_ = State::Source;
    std::vector<core::Vec2> picks_;
    bool done_ = false;
};

// MATCHPROP / MA: pick a source object, then pick destination object(s) or [Settings].
// Each destination immediately adopts the source's matched properties (universal always;
// family-scoped only within a shared family). A paintbrush cursor is shown while picking
// targets, and each matched target is its own undo entry. Pick-based like JOIN.
class MatchPropCommand final : public ICommand {
public:
    std::string name() const override { return "MATCHPROP"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Source, Targets } state_ = State::Source;
    bool done_ = false;
};

// HATCH / H: fill a closed boundary with a pattern (Part A: SOLID, from selected closed
// polylines). Noun-verb: with a pre-selection, hatches it immediately; otherwise prompts to
// pick a closed boundary. The engine extracts the boundary loops (UI never touches the store).
class HatchCommand final : public ICommand {
public:
    std::string name() const override { return "HATCH"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class Mode { PickPoint, Pattern, Scale, Angle };
    bool done_ = false;
    Mode mode_ = Mode::PickPoint;
    std::string pattern_ = "SOLID"; // SOLID or a known line pattern (e.g. ANSI31)
    double scale_ = 1.0;
    double angle_ = 0.0; // radians (pattern rotation)
};

/// TOLERANCE / TOL -- a GD&T feature control frame. Command-line Q&A: the cells are
/// typed one at a time (the characteristic first), Enter on an empty line finishes the
/// list, then a pick places the frame. The ribbon button opens the Ph11 ParameterDialog
/// instead, which is the case the "dialogs when a dialog genuinely fits" rule
/// contemplates -- an FCF is genuinely multi-parameter. Both end at AddFcfCommand.
class ToleranceCommand final : public ICommand {
public:
    std::string name() const override { return "TOLERANCE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class Mode { Cells, Place };
    void prompt_cell(CommandContext& ctx);
    bool done_ = false;
    Mode mode_ = Mode::Cells;
    std::vector<std::string> cells_;
};

/// DATUM / DIMDATUM -- a datum feature symbol: the letter, the point on the feature,
/// then the box placement.
class DatumCommand final : public ICommand {
public:
    std::string name() const override { return "DATUM"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class Mode { Letter, Tip, Place };
    bool done_ = false;
    Mode mode_ = Mode::Letter;
    std::string letter_ = "A";
    core::Vec2 tip_{};
};

/// TABLE / TB -- insert a table (issue #22). Command-line Q&A: rows, columns, column
/// width, row height, then a placement pick. Cells start empty; they are filled by
/// editing, which keeps the command a placement tool rather than a data-entry form.
/// STRETCH / S -- move the vertices inside a crossing window (issue #24). AutoCAD insists
/// on a crossing window for this command, so the command asks for one rather than
/// consuming the current selection: "which points move" is the window's job, and a
/// pre-existing selection cannot express it.
/// Inquiry commands (issue #30). DIST and ID answer from the picked points alone, so
/// they never reach the store; AREA and LIST submit a query the geometry thread resolves
/// and reports through the status channel.
/// DIMCONTINUE (DCO) / DIMBASELINE (DBA) -- issue #28. Each pick adds another dimension
/// chained from the previous one, so the command loops until Esc/Enter, which is how a
/// row of holes actually gets dimensioned.
class ChainDimCommand final : public ICommand {
public:
    explicit ChainDimCommand(bool baseline) : baseline_(baseline) {}
    std::string name() const override { return baseline_ ? "DIMBASELINE" : "DIMCONTINUE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool baseline_ = false;
    bool done_ = false;
};

class DistCommand final : public ICommand {
public:
    std::string name() const override { return "DIST"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
    bool have_first_ = false;
    core::Vec2 first_{};
};

class IdCommand final : public ICommand {
public:
    std::string name() const override { return "ID"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class AreaCommand final : public ICommand {
public:
    std::string name() const override { return "AREA"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class ListCommand final : public ICommand {
public:
    std::string name() const override { return "LIST"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class StretchCommand final : public ICommand {
public:
    std::string name() const override { return "STRETCH"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    /// The two crossing-window corners are region picks: no osnap, no ortho/polar.
    bool wants_window() const override {
        return mode_ == Mode::Corner1 || mode_ == Mode::Corner2;
    }

private:
    enum class Mode { Corner1, Corner2, Base, Displacement };
    bool done_ = false;
    Mode mode_ = Mode::Corner1;
    core::Vec2 c1_{};
    core::Vec2 c2_{};
    core::Vec2 base_{};
};

class TableCommand final : public ICommand {
public:
    std::string name() const override { return "TABLE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class Mode { Rows, Cols, ColWidth, RowHeight, Place };
    bool done_ = false;
    Mode mode_ = Mode::Rows;
    int rows_ = 4;
    int cols_ = 3;
    double col_w_ = 40.0;
    double row_h_ = 8.0;
};

class TrimCommand final : public ICommand {
public:
    std::string name() const override { return "TRIM"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class RotateCommand final : public ICommand {
public:
    std::string name() const override { return "ROTATE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::optional<core::Vec2> base_;
    bool done_ = false;
};

class ScaleCommand final : public ICommand {
public:
    std::string name() const override { return "SCALE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::optional<core::Vec2> base_;
    bool done_ = false;
};

/// PURGE (AutoCAD PU). No prompts: an imported drawing's unused layers just go, and the
/// engine reports how many. Undo is deliberately NOT offered -- purging removes symbol
/// table entries nothing refers to, so there is no geometry to restore.
class PurgeCommand final : public ICommand {
public:
    std::string name() const override { return "PURGE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// ALIGN (AutoCAD AL): fit the selection between two known points in one step, with an
/// optional uniform scale. Two source/destination pairs, then the scale question --
/// AutoCAD's 2D flow, without the third pair that only means anything in 3D.
class AlignCommand final : public ICommand {
public:
    std::string name() const override { return "ALIGN"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Src1, Dst1, Src2, Dst2, Scale };
    State state_ = State::Src1;
    core::Vec2 src1_{};
    core::Vec2 dst1_{};
    core::Vec2 src2_{};
    core::Vec2 dst2_{};
    bool done_ = false;
};

/// LENGTHEN (AutoCAD LEN): change the length of a line or arc. The mode is chosen
/// first, as in AutoCAD, then the amount, then the object -- and the end nearer the
/// pick is the one that moves.
class LengthenCommand final : public ICommand {
public:
    std::string name() const override { return "LENGTHEN"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Mode, Amount, Pick };
    State state_ = State::Mode;
    core::LengthenCommand::Mode mode_ = core::LengthenCommand::Mode::Total;
    double value_ = 0.0;
    bool done_ = false;
};

/// BREAK (AutoCAD BR) and BREAKATPOINT.
///
/// AutoCAD's flow is unusual and worth keeping: the pick that SELECTS the object is
/// also the first break point, so the common case is two clicks. `First point` re-asks
/// for it when the selecting click was not where the break should be.
class BreakCommand final : public ICommand {
public:
    /// `at_point` true = BREAKATPOINT: split with no gap, one point only.
    explicit BreakCommand(bool at_point = false) : at_point_(at_point) {}

    std::string name() const override { return at_point_ ? "BREAKATPOINT" : "BREAK"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Select, Second, FirstAgain };
    bool at_point_ = false;
    State state_ = State::Select;
    core::Vec2 pick_{};
    core::Vec2 p1_{};
    bool done_ = false;
};

/// POLYGON (AutoCAD POL). A regular n-gon, committed as an ordinary closed polyline.
///
/// Follows AutoCAD's two ways of sizing one: about a CENTRE, where the cursor distance
/// is either the circumradius (Inscribed) or the apothem (Circumscribed), or by one
/// EDGE, where two picks give a side and the polygon is built to its left.
class PolygonCommand final : public ICommand {
public:
    std::string name() const override { return "POLYGON"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Sides, Center, Fit, Radius, Edge1, Edge2 };
    void refresh_preview(CommandContext& ctx);

    State state_ = State::Sides;
    int sides_ = 4;
    bool inscribed_ = true;
    core::Vec2 center_{};
    core::Vec2 edge1_{};
    bool done_ = false;
};

/// POINT (AutoCAD PO). Places a POINT entity at each pick and keeps going until Esc,
/// which is what AutoCAD does -- points are almost always placed in groups.
class PointCommand final : public ICommand {
public:
    std::string name() const override { return "POINT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// DIVIDE and MEASURE (AutoCAD DIV / ME). Both pick a curve and then place POINT marks
/// along it; they differ only in whether the second answer is a segment COUNT or a
/// segment LENGTH, so they share one state machine.
class DivideCommand final : public ICommand {
public:
    /// `measure` false = DIVIDE (into N equal parts), true = MEASURE (every N units).
    explicit DivideCommand(bool measure = false) : measure_(measure) {}

    std::string name() const override { return measure_ ? "MEASURE" : "DIVIDE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Pick, Amount };
    bool measure_ = false;
    State state_ = State::Pick;
    core::Vec2 pick_{};
    bool done_ = false;
};

/// The whole AutoCAD array family in one state machine.
///
/// ARRAY (AR) and -ARRAY ask for the type, exactly as AutoCAD does; ARRAYRECT,
/// ARRAYPOLAR and ARRAYPATH jump straight to their own prompts. One class because the
/// three types differ only in which prompts they ask -- splitting them would duplicate
/// the selection guard, the parsing and the cancel path three ways.
///
/// ARRAYEDIT and ARRAYCLOSE are deliberately absent: both edit an ASSOCIATIVE array,
/// which is a parametric entity that remembers its source and parameters. Musa CAD's
/// arrays are non-associative -- the same thing AutoCAD's own -ARRAY produces -- so
/// there is no association to reopen. See docs/COMMANDS.md.
class ArrayCommand final : public ICommand {
public:
    /// Which prompts to ask. `Ask` is ARRAY/-ARRAY; the rest are the direct commands.
    enum class Type { Ask, Rect, Polar, Path };

    explicit ArrayCommand(Type type = Type::Ask) : type_(type) {}

    std::string name() const override {
        switch (type_) {
        case Type::Rect:
            return "ARRAYRECT";
        case Type::Polar:
            return "ARRAYPOLAR";
        case Type::Path:
            return "ARRAYPATH";
        case Type::Ask:
            break;
        }
        return "ARRAY";
    }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    /// The path pick selects a curve, but it is a COORDINATE pick here (the engine
    /// resolves the curve from the point), so the normal snap rules apply.
    bool wants_selection() const override { return false; }

private:
    enum class State {
        Type,
        // Rectangular
        Rows,
        Cols,
        RowSpace,
        ColSpace,
        Angle,
        // Polar
        Center,
        Count,
        Fill,
        RotateItems,
        // Path
        PathPick,
        PathMethod,
        PathCount,
        PathSpacing,
        PathAlign,
    };
    void begin_rect(CommandContext& ctx);
    void begin_polar(CommandContext& ctx);
    void begin_path(CommandContext& ctx);

    Type type_ = Type::Ask;
    State state_ = State::Type;
    int rows_ = 1;
    int cols_ = 1;
    double row_space_ = 0.0;
    double col_space_ = 0.0;
    int count_ = 1;
    double fill_ = 0.0;
    core::Vec2 center_{};
    // Path
    core::Vec2 path_pick_{};
    double path_spacing_ = 0.0;
    bool done_ = false;
};

class ExtendCommand final : public ICommand {
public:
    std::string name() const override { return "EXTEND"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class FilletCommand final : public ICommand {
public:
    std::string name() const override { return "FILLET"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Radius, First, Second } state_ = State::Radius;
    double radius_ = 0.0;
    core::Vec2 pick1_{};
    bool done_ = false;
};

class ChamferCommand final : public ICommand {
public:
    std::string name() const override { return "CHAMFER"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    // Distance method (Dist1->Dist2) or Angle method (AngleLen->AngleVal, default
    // 45 degrees), then pick the two lines.
    enum class State { Dist1, Dist2, AngleLen, AngleVal, First, Second } state_ = State::Dist1;
    double dist1_ = 0.0;
    double dist2_ = 0.0;
    double length_ = 0.0; // chamfer length on the first line (Angle method)
    core::Vec2 pick1_{};
    bool done_ = false;
};

// --- Annotation (Phase 13) -------------------------------------------------

class TextCommand final : public ICommand {
public:
    std::string name() const override { return "TEXT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Point, Height, Rotation, Content } state_ = State::Point;
    core::Vec2 pos_{};
    double height_ = 2.5;
    double rotation_ = 0.0;
    bool done_ = false;
};

/// DIMLINEAR / DIMALIGNED share one state machine, parameterised by type/name.
class LinearDimensionCommand final : public ICommand {
public:
    LinearDimensionCommand(core::DimType type, std::string name)
        : type_(type), name_(std::move(name)) {}
    std::string name() const override { return name_; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    // Two-point flow: First -> Second -> Place. Object flow (via the [Object]
    // keyword or an empty first input): SelectObj -> ObjPlace.
    enum class State { First, Second, Place, SelectObj, ObjPlace } state_ = State::First;
    core::DimType type_;
    std::string name_;
    core::Vec2 a_{};
    core::Vec2 b_{};
    core::Vec2 obj_pick_{};
    bool done_ = false;
};

/// DIMRADIUS / DIMDIAMETER: select a circle or arc, then place the dimension line.
/// The value comes from the entity's own centre + radius (object-aware).
class RadialDimensionCommand final : public ICommand {
public:
    RadialDimensionCommand(core::DimType type, std::string name)
        : type_(type), name_(std::move(name)) {}
    std::string name() const override { return name_; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Select, Place } state_ = State::Select;
    core::DimType type_;
    std::string name_;
    core::Vec2 obj_pick_{};
    bool done_ = false;
};

/// DIMANGULAR: select two lines (or polyline edges); the angle is read from the
/// entities' directions (object-aware).
class AngularDimensionCommand final : public ICommand {
public:
    std::string name() const override { return "DIMANGULAR"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Line1, Line2, Place } state_ = State::Line1;
    core::Vec2 pick1_{};
    core::Vec2 pick2_{};
    bool done_ = false;
};

/// DIM: AutoCAD's smart all-in-one dimension. As the cursor moves over candidates
/// it previews the type it would create; on pick it reads the hovered entity kind
/// (circle -> diameter, arc -> radius, line/polyline -> linear) and dispatches to
/// the SAME object-aware machinery as DIMRADIUS/DIMDIAMETER/DIMLINEAR.
class DimCommand final : public ICommand {
public:
    std::string name() const override { return "DIM"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void hover(CommandContext& ctx, std::optional<core::EntityKind> kind) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Select, Place } state_ = State::Select;
    core::DimType type_ = core::DimType::Linear;
    core::Vec2 obj_pick_{};
    bool done_ = false;
};

/// LEADER: pick the arrow tip, the landing point, then enter the label.
class LeaderCommand final : public ICommand {
public:
    std::string name() const override { return "LEADER"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Tip, Knee, Content } state_ = State::Tip;
    core::Vec2 tip_{};
    core::Vec2 knee_{};
    bool done_ = false;
};

/// MTEXT (MT/T): pick two corners (insertion + wrap width), then enter paragraph
/// text. Wraps within the defined width across multiple lines.
class MTextCommand final : public ICommand {
public:
    std::string name() const override { return "MTEXT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { First, Second, Content } state_ = State::First;
    core::Vec2 c1_{};
    core::Vec2 pos_{};
    double width_ = 0.0;
    bool done_ = false;
};

/// QLEADER (LE/QLEADER): pick the arrow point, then leader vertices (Enter to
/// finish), then enter the annotation (MTEXT). Arrow + leader line + attached text.
class QLeaderCommand final : public ICommand {
public:
    std::string name() const override { return "QLEADER"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Arrow, Vertices, Content } state_ = State::Arrow;
    std::vector<core::Vec2> verts_;
    bool done_ = false;
};

/// TEXTEDIT / DDEDIT (ED): pick a text-bearing entity (TEXT / MTEXT / QLEADER
/// label), then type the new content. The scriptable/keyboard path to text edit;
/// same one-undo-group content change as the double-click editor.
class TextEditCommand final : public ICommand {
public:
    std::string name() const override { return "TEXTEDIT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Pick, Content } state_ = State::Pick;
    core::Vec2 at_{};
    double radius_ = 0.0;
    bool done_ = false;
};

/// LTSCALE: set the global linetype scale (drawing-wide). Prompts for the factor,
/// then submits SetLtscaleCommand; all non-continuous entities re-dash live.
class LtscaleCommand final : public ICommand {
public:
    std::string name() const override { return "LTSCALE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// DWGIN / DWGOUT: one-shot view commands that trigger the external-converter DWG
/// import/export via ViewControl (the MainWindow owns the file dialog + converter).
class DwgInCommand final : public ICommand {
public:
    std::string name() const override { return "DWGIN"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class DwgOutCommand final : public ICommand {
public:
    std::string name() const override { return "DWGOUT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// PR / PROPERTIES / PROPS / CH: toggle the Properties palette. A one-shot view
/// command -- it opens the panel via ViewControl and finishes immediately.
class PropertiesCommand final : public ICommand {
public:
    std::string name() const override { return "PROPERTIES"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

/// PLOT / PRINT: open the plot dialog (PDF + printer). One-shot view command.
class PlotCommand final : public ICommand {
public:
    std::string name() const override { return "PLOT"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext&, const std::string&) override {}
    void cancel(CommandContext&) override { done_ = true; }
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

} // namespace musacad::command
