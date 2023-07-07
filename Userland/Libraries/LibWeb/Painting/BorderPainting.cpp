/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021-2022, Sam Atkins <atkinssj@serenityos.org>
 * Copyright (c) 2022, MacDue <macdue@dueutil.tech>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/AntiAliasingPainter.h>
#include <LibGfx/Painter.h>
#include <LibGfx/Path.h>
#include <LibWeb/Painting/BorderPainting.h>
#include <LibWeb/Painting/PaintContext.h>

namespace Web::Painting {

BorderRadiiData normalized_border_radii_data(Layout::Node const& node, CSSPixelRect const& rect, CSS::BorderRadiusData top_left_radius, CSS::BorderRadiusData top_right_radius, CSS::BorderRadiusData bottom_right_radius, CSS::BorderRadiusData bottom_left_radius)
{
    BorderRadiusData bottom_left_radius_px {};
    BorderRadiusData bottom_right_radius_px {};
    BorderRadiusData top_left_radius_px {};
    BorderRadiusData top_right_radius_px {};

    bottom_left_radius_px.horizontal_radius = bottom_left_radius.horizontal_radius.to_px(node, rect.width());
    bottom_right_radius_px.horizontal_radius = bottom_right_radius.horizontal_radius.to_px(node, rect.width());
    top_left_radius_px.horizontal_radius = top_left_radius.horizontal_radius.to_px(node, rect.width());
    top_right_radius_px.horizontal_radius = top_right_radius.horizontal_radius.to_px(node, rect.width());

    bottom_left_radius_px.vertical_radius = bottom_left_radius.vertical_radius.to_px(node, rect.height());
    bottom_right_radius_px.vertical_radius = bottom_right_radius.vertical_radius.to_px(node, rect.height());
    top_left_radius_px.vertical_radius = top_left_radius.vertical_radius.to_px(node, rect.height());
    top_right_radius_px.vertical_radius = top_right_radius.vertical_radius.to_px(node, rect.height());

    // Scale overlapping curves according to https://www.w3.org/TR/css-backgrounds-3/#corner-overlap
    CSSPixels f = 1.0f;
    auto width_reciprocal = 1.0 / rect.width().to_double();
    auto height_reciprocal = 1.0 / rect.height().to_double();
    f = max(f, width_reciprocal * (top_left_radius_px.horizontal_radius + top_right_radius_px.horizontal_radius));
    f = max(f, height_reciprocal * (top_right_radius_px.vertical_radius + bottom_right_radius_px.vertical_radius));
    f = max(f, width_reciprocal * (bottom_left_radius_px.horizontal_radius + bottom_right_radius_px.horizontal_radius));
    f = max(f, height_reciprocal * (top_left_radius_px.vertical_radius + bottom_left_radius_px.vertical_radius));

    f = 1.0 / f.to_double();

    top_left_radius_px.horizontal_radius *= f;
    top_left_radius_px.vertical_radius *= f;
    top_right_radius_px.horizontal_radius *= f;
    top_right_radius_px.vertical_radius *= f;
    bottom_right_radius_px.horizontal_radius *= f;
    bottom_right_radius_px.vertical_radius *= f;
    bottom_left_radius_px.horizontal_radius *= f;
    bottom_left_radius_px.vertical_radius *= f;

    return BorderRadiiData { top_left_radius_px, top_right_radius_px, bottom_right_radius_px, bottom_left_radius_px };
}

void paint_border(PaintContext& context, BorderEdge edge, DevicePixelRect const& rect, Gfx::AntiAliasingPainter::CornerRadius const& radius, Gfx::AntiAliasingPainter::CornerRadius const& opposite_radius, BordersData const& borders_data)
{
    auto const& border_data = [&] {
        switch (edge) {
        case BorderEdge::Top:
            return borders_data.top;
        case BorderEdge::Right:
            return borders_data.right;
        case BorderEdge::Bottom:
            return borders_data.bottom;
        default: // BorderEdge::Left:
            return borders_data.left;
        }
    }();

    CSSPixels width = border_data.width;
    if (width <= 0)
        return;

    auto color = border_data.color;
    auto border_style = border_data.line_style;
    auto device_pixel_width = context.enclosing_device_pixels(width);

    struct Points {
        DevicePixelPoint p1;
        DevicePixelPoint p2;
    };

    auto points_for_edge = [](BorderEdge edge, DevicePixelRect const& rect) -> Points {
        switch (edge) {
        case BorderEdge::Top:
            return { rect.top_left(), rect.top_right().moved_left(1) };
        case BorderEdge::Right:
            return { rect.top_right().moved_left(1), rect.bottom_right().translated(-1) };
        case BorderEdge::Bottom:
            return { rect.bottom_left().moved_up(1), rect.bottom_right().translated(-1) };
        default: // Edge::Left
            return { rect.top_left(), rect.bottom_left().moved_up(1) };
        }
    };

    if (border_style == CSS::LineStyle::Inset) {
        auto top_left_color = Color::from_rgb(0x5a5a5a);
        auto bottom_right_color = Color::from_rgb(0x888888);
        color = (edge == BorderEdge::Left || edge == BorderEdge::Top) ? top_left_color : bottom_right_color;
    } else if (border_style == CSS::LineStyle::Outset) {
        auto top_left_color = Color::from_rgb(0x888888);
        auto bottom_right_color = Color::from_rgb(0x5a5a5a);
        color = (edge == BorderEdge::Left || edge == BorderEdge::Top) ? top_left_color : bottom_right_color;
    }

    auto gfx_line_style = Gfx::Painter::LineStyle::Solid;
    if (border_style == CSS::LineStyle::Dotted)
        gfx_line_style = Gfx::Painter::LineStyle::Dotted;
    if (border_style == CSS::LineStyle::Dashed)
        gfx_line_style = Gfx::Painter::LineStyle::Dashed;

    if (gfx_line_style != Gfx::Painter::LineStyle::Solid) {
        auto [p1, p2] = points_for_edge(edge, rect);
        switch (edge) {
        case BorderEdge::Top:
            p1.translate_by(device_pixel_width / 2, device_pixel_width / 2);
            p2.translate_by(-device_pixel_width / 2, device_pixel_width / 2);
            break;
        case BorderEdge::Right:
            p1.translate_by(-device_pixel_width / 2, device_pixel_width / 2);
            p2.translate_by(-device_pixel_width / 2, -device_pixel_width / 2);
            break;
        case BorderEdge::Bottom:
            p1.translate_by(device_pixel_width / 2, -device_pixel_width / 2);
            p2.translate_by(-device_pixel_width / 2, -device_pixel_width / 2);
            break;
        case BorderEdge::Left:
            p1.translate_by(device_pixel_width / 2, device_pixel_width / 2);
            p2.translate_by(device_pixel_width / 2, -device_pixel_width / 2);
            break;
        }
        if (border_style == CSS::LineStyle::Dotted) {
            Gfx::AntiAliasingPainter aa_painter { context.painter() };
            aa_painter.draw_line(p1.to_type<int>(), p2.to_type<int>(), color, device_pixel_width.value(), gfx_line_style);
            return;
        }
        context.painter().draw_line(p1.to_type<int>(), p2.to_type<int>(), color, device_pixel_width.value(), gfx_line_style);
        return;
    }

    auto draw_border = [&](Vector<Gfx::FloatPoint> const& points, bool left, bool right) {
        Gfx::AntiAliasingPainter aa_painter { context.painter() };
        Gfx::Path path;
        int current = 0;
        path.move_to(points[current++]);
        path.elliptical_arc_to(
            points[current++],
            // Gfx::FloatPoint(rect.top_left().moved_left(radius.horizontal_radius / AK::sqrt(2.0f)).x().value(), rect.top_left().moved_down(radius.vertical_radius * 1.0f - radius.vertical_radius / AK::sqrt(2.0f)).y().value()),
            // Gfx::FloatPoint(rect.top_left().x().value(), rect.top_left().moved_down(radius.vertical_radius).y().value()),
            Gfx::FloatSize(radius.horizontal_radius, radius.vertical_radius),
            0,
            // 0.01,
            // 0.01,
            false,
            false);
        path.line_to(points[current++]);
        if (left) {
            path.elliptical_arc_to(
                points[current++],
                // Gfx::FloatPoint(rect.top_left().moved_left(radius.horizontal_radius / AK::sqrt(2.0f)).x().value(), rect.top_left().moved_down(radius.vertical_radius * 1.0f - radius.vertical_radius / AK::sqrt(2.0f)).y().value()),
                // Gfx::FloatPoint(rect.top_left().x().value(), rect.top_left().moved_down(radius.vertical_radius).y().value()),
                Gfx::FloatSize(radius.horizontal_radius - 10, radius.vertical_radius - 10),
                AK::Pi<double>,
                // 0.01,
                // 0.01,
                false,
                true);
        }
        path.line_to(points[current++]);
        if (right) {
            path.elliptical_arc_to(
                points[current++],
                // Gfx::FloatPoint(rect.top_left().moved_left(radius.horizontal_radius / AK::sqrt(2.0f)).x().value(), rect.top_left().moved_down(radius.vertical_radius * 1.0f - radius.vertical_radius / AK::sqrt(2.0f)).y().value()),
                // Gfx::FloatPoint(rect.top_left().x().value(), rect.top_left().moved_down(radius.vertical_radius).y().value()),
                Gfx::FloatSize(radius.horizontal_radius - 10, radius.vertical_radius - 10),
                0,
                // 0.01,
                // 0.01,
                false,
                true);
        }
        path.line_to(points[current++]);
        path.elliptical_arc_to(
            points[current++],
            // Gfx::FloatPoint(rect.top_left().moved_left(radius.horizontal_radius / AK::sqrt(2.0f)).x().value(), rect.top_left().moved_down(radius.vertical_radius * 1.0f - radius.vertical_radius / AK::sqrt(2.0f)).y().value()),
            // Gfx::FloatPoint(rect.top_left().x().value(), rect.top_left().moved_down(radius.vertical_radius).y().value()),
            Gfx::FloatSize(opposite_radius.horizontal_radius, opposite_radius.vertical_radius),
            AK::Pi<double>,
            // 0.01,
            // 0.01,
            false,
            false);
        path.close();
        aa_painter.fill_path(path, color, Gfx::Painter::WindingRule::EvenOdd);
    };

    switch (edge) {
    case BorderEdge::Top: {
        Gfx::FloatPoint corner_offset_1;
        Gfx::FloatPoint corner_offset_2;
        if (borders_data.left.width == 0) {
            corner_offset_1 = Gfx::FloatPoint(-radius.horizontal_radius, radius.vertical_radius);
        } else {
            corner_offset_1 = Gfx::FloatPoint(-radius.horizontal_radius * AK::sin<float>(AK::Pi<float> * 0.25), radius.vertical_radius * (1 - AK::cos(AK::Pi<float> * 0.25)));
        }

        if (borders_data.right.width == 0) {
            corner_offset_2 = Gfx::FloatPoint(opposite_radius.horizontal_radius, opposite_radius.vertical_radius);
        } else {
            corner_offset_2 = Gfx::FloatPoint(opposite_radius.horizontal_radius / AK::sqrt(2.0f), opposite_radius.vertical_radius * (1 - 1 / AK::sqrt(2.0f)));
        }

        Vector<Gfx::FloatPoint> points;
        points.ensure_capacity(8);
        points.append(Gfx::FloatPoint(rect.top_left().to_type<int>()));
        points.append(Gfx::FloatPoint(rect.top_left().to_type<int>()) + corner_offset_1);

        bool left = false;
        bool right = false;
        if (device_pixel_width.value() < radius.vertical_radius) {
            left = true;
            dbgln("width is {}, radius is {}", context.enclosing_device_pixels(borders_data.top.width).value(), radius.vertical_radius);
            Gfx::FloatPoint inner_corner = Gfx::FloatPoint(-(radius.horizontal_radius - context.enclosing_device_pixels(borders_data.top.width).value()) * AK::sin<float>(AK::Pi<float> * 0.25), (radius.vertical_radius - context.enclosing_device_pixels(borders_data.top.width).value()) * (1 - AK::cos(AK::Pi<float> * 0.25)));
            points.append(Gfx::FloatPoint(rect.bottom_left().to_type<int>()) + inner_corner);
            points.append(Gfx::FloatPoint(rect.bottom_left().to_type<int>()));
        } else {
            Gfx::FloatPoint border_corner_gap_1 = Gfx::FloatPoint(context.enclosing_device_pixels(borders_data.left.width).value() - radius.horizontal_radius, 0);
            points.append(Gfx::FloatPoint(rect.bottom_left().to_type<int>()) + border_corner_gap_1);
        }

        if (device_pixel_width.value() < opposite_radius.vertical_radius) {
            right = true;
            Gfx::FloatPoint inner_corner = Gfx::FloatPoint((opposite_radius.horizontal_radius - context.enclosing_device_pixels(borders_data.top.width).value()) * AK::sin<float>(AK::Pi<float> * 0.25), (opposite_radius.vertical_radius - context.enclosing_device_pixels(borders_data.top.width).value()) * (1 - AK::cos(AK::Pi<float> * 0.25)));
            points.append(Gfx::FloatPoint(rect.bottom_right().to_type<int>()));
            points.append(Gfx::FloatPoint(rect.bottom_right().to_type<int>()) + inner_corner);
        } else {
            Gfx::FloatPoint border_corner_gap_2 = Gfx::FloatPoint(context.enclosing_device_pixels(borders_data.right.width).value() - opposite_radius.horizontal_radius, 0);
            points.append(Gfx::FloatPoint(rect.bottom_right().to_type<int>()) - border_corner_gap_2);
        }

        points.append(Gfx::FloatPoint(rect.top_right().to_type<int>()) + corner_offset_2);
        points.append(Gfx::FloatPoint(rect.top_right().to_type<int>()));
        draw_border(points, left, right);
        break;
    }
    case BorderEdge::Right: {
        Gfx::FloatPoint corner_offset_1;
        Gfx::FloatPoint corner_offset_2;
        if (borders_data.top.width == 0) {
            corner_offset_1 = Gfx::FloatPoint(-radius.horizontal_radius, -radius.vertical_radius);
        } else {
            corner_offset_1 = Gfx::FloatPoint(-radius.vertical_radius * (1 - 1 / AK::sqrt(2.0f)), -radius.horizontal_radius / AK::sqrt(2.0f));
        }

        if (borders_data.bottom.width == 0) {
            corner_offset_2 = Gfx::FloatPoint(-opposite_radius.horizontal_radius, opposite_radius.vertical_radius);
        } else {
            corner_offset_2 = Gfx::FloatPoint(-opposite_radius.vertical_radius * (1 - 1 / AK::sqrt(2.0f)), opposite_radius.horizontal_radius / AK::sqrt(2.0f));
        }

        Vector<Gfx::FloatPoint> points;
        points.ensure_capacity(6);
        points.append(Gfx::FloatPoint(rect.top_right().to_type<int>()));
        points.append(Gfx::FloatPoint(rect.top_right().to_type<int>()) + corner_offset_1);
        bool left = false;
        bool right = false;
        if (device_pixel_width < radius.horizontal_radius) {
            left = true;
            Gfx::FloatPoint inner_corner = Gfx::FloatPoint(-(radius.horizontal_radius - device_pixel_width.value()) * (1 - AK::cos(AK::Pi<float> * 0.25)), -(radius.horizontal_radius - device_pixel_width.value()) * AK::sin<float>(AK::Pi<float> * 0.25));
            points.append(Gfx::FloatPoint(rect.top_left().to_type<int>()) + inner_corner);
            points.append(Gfx::FloatPoint(rect.top_left().to_type<int>()));
        } else {
            Gfx::FloatPoint border_corner_gap_1 = Gfx::FloatPoint(0, context.enclosing_device_pixels(borders_data.top.width).value() - radius.horizontal_radius);
            points.append(Gfx::FloatPoint(rect.top_left().to_type<int>()) + border_corner_gap_1);
        }

        if (device_pixel_width < opposite_radius.horizontal_radius) {
            right = true;
            Gfx::FloatPoint inner_corner = Gfx::FloatPoint(-(radius.horizontal_radius - device_pixel_width.value()) * (1 - AK::cos(AK::Pi<float> * 0.25)), (radius.horizontal_radius - device_pixel_width.value()) * AK::sin<float>(AK::Pi<float> * 0.25));
            points.append(Gfx::FloatPoint(rect.bottom_left().to_type<int>()));
            points.append(Gfx::FloatPoint(rect.bottom_left().to_type<int>()) + inner_corner);
        } else {
            Gfx::FloatPoint border_corner_gap_2 = Gfx::FloatPoint(0, context.enclosing_device_pixels(borders_data.bottom.width).value() - opposite_radius.horizontal_radius);
            points.append(Gfx::FloatPoint(rect.bottom_left().to_type<int>()) - border_corner_gap_2);
        }

        points.append(Gfx::FloatPoint(rect.bottom_right().to_type<int>()) + corner_offset_2);
        points.append(Gfx::FloatPoint(rect.bottom_right().to_type<int>()));
        draw_border(points, left, right);
        break;
    }
    case BorderEdge::Bottom: {
        Gfx::FloatPoint corner_offset_1;
        Gfx::FloatPoint corner_offset_2;
        if (borders_data.right.width == 0) {
            corner_offset_1 = Gfx::FloatPoint(radius.horizontal_radius, -radius.vertical_radius);
        } else {
            corner_offset_1 = Gfx::FloatPoint(radius.horizontal_radius / AK::sqrt(2.0f), -radius.vertical_radius * (1 - 1 / AK::sqrt(2.0f)));
        }

        if (borders_data.left.width == 0) {
            corner_offset_2 = Gfx::FloatPoint(-opposite_radius.horizontal_radius, -opposite_radius.vertical_radius);
        } else {
            corner_offset_2 = Gfx::FloatPoint(-opposite_radius.horizontal_radius / AK::sqrt(2.0f), -opposite_radius.vertical_radius * (1 - 1 / AK::sqrt(2.0f)));
        }

        Vector<Gfx::FloatPoint> points;
        points.ensure_capacity(6);
        points.append(Gfx::FloatPoint(rect.bottom_right().to_type<int>()));
        points.append(Gfx::FloatPoint(rect.bottom_right().to_type<int>()) + corner_offset_1);

        bool left = false;
        bool right = false;
        if (device_pixel_width < radius.vertical_radius) {
            left = true;
            Gfx::FloatPoint inner_corner = Gfx::FloatPoint((radius.vertical_radius - device_pixel_width.value()) * AK::sin<float>(AK::Pi<float> * 0.25), -(radius.vertical_radius - device_pixel_width.value()) * (1 - AK::cos(AK::Pi<float> * 0.25)));
            points.append(Gfx::FloatPoint(rect.top_right().to_type<int>()) + inner_corner);
            points.append(Gfx::FloatPoint(rect.top_right().to_type<int>()));
        } else {
            Gfx::FloatPoint border_corner_gap_1 = Gfx::FloatPoint(context.enclosing_device_pixels(borders_data.right.width).value() - radius.horizontal_radius, 0);
            points.append(Gfx::FloatPoint(rect.top_right().to_type<int>()) - border_corner_gap_1);
        }

        if (device_pixel_width < opposite_radius.vertical_radius) {
            right = true;
            Gfx::FloatPoint inner_corner = Gfx::FloatPoint(-(opposite_radius.vertical_radius - device_pixel_width.value()) * AK::sin<float>(AK::Pi<float> * 0.25), -(opposite_radius.vertical_radius - device_pixel_width.value()) * (1 - AK::cos(AK::Pi<float> * 0.25)));
            points.append(Gfx::FloatPoint(rect.top_left().to_type<int>()));
            points.append(Gfx::FloatPoint(rect.top_left().to_type<int>()) + inner_corner);
        } else {
            Gfx::FloatPoint border_corner_gap_2 = Gfx::FloatPoint(context.enclosing_device_pixels(borders_data.left.width).value() - opposite_radius.horizontal_radius, 0);
            points.append(Gfx::FloatPoint(rect.top_left().to_type<int>()) + border_corner_gap_2);
        }
        points.append(Gfx::FloatPoint(rect.bottom_left().to_type<int>()) + corner_offset_2);
        points.append(Gfx::FloatPoint(rect.bottom_left().to_type<int>()));
        draw_border(points, left, right);
        break;
    }
    case BorderEdge::Left: {
        Gfx::AntiAliasingPainter aa_painter { context.painter() };
        Gfx::FloatPoint corner_offset_1;
        Gfx::FloatPoint corner_offset_2;
        bool left = false;
        bool right = false;
        if (borders_data.bottom.width == 0) {
            corner_offset_1 = Gfx::FloatPoint(radius.horizontal_radius, radius.vertical_radius);
        } else {
            corner_offset_1 = Gfx::FloatPoint(radius.horizontal_radius * (1 - 1 / AK::sqrt(2.0f)), radius.vertical_radius / AK::sqrt(2.0f));
        }

        if (borders_data.top.width == 0) {
            corner_offset_2 = Gfx::FloatPoint(opposite_radius.horizontal_radius, opposite_radius.vertical_radius);
        } else {
            corner_offset_2 = Gfx::FloatPoint(opposite_radius.horizontal_radius * (1 - 1 / AK::sqrt(2.0f)), -opposite_radius.vertical_radius / AK::sqrt(2.0f));
        }

        Gfx::FloatPoint border_corner_gap_2 = Gfx::FloatPoint(0, context.enclosing_device_pixels(borders_data.top.width).value() - opposite_radius.vertical_radius);

        Vector<Gfx::FloatPoint> points;
        points.append(Gfx::FloatPoint(rect.bottom_left().to_type<int>()));
        points.append(Gfx::FloatPoint(rect.bottom_left().to_type<int>()) + corner_offset_1);

        if (device_pixel_width < radius.vertical_radius) {
            left = true;
            Gfx::FloatPoint inner_corner = Gfx::FloatPoint((radius.vertical_radius - device_pixel_width.value()) * (1 - AK::cos(AK::Pi<float> * 0.25)), (radius.horizontal_radius - device_pixel_width.value()) * AK::sin<float>(AK::Pi<float> * 0.25));
            points.append(Gfx::FloatPoint(rect.bottom_right().to_type<int>()) + inner_corner);
            points.append(Gfx::FloatPoint(rect.bottom_right().to_type<int>()));
        } else {
            Gfx::FloatPoint border_corner_gap_1 = Gfx::FloatPoint(0, context.enclosing_device_pixels(borders_data.bottom.width).value() - radius.vertical_radius);
            points.append(Gfx::FloatPoint(rect.bottom_right().to_type<int>()) - border_corner_gap_1);
        }

        if (device_pixel_width < opposite_radius.vertical_radius) {
            right = true;
            Gfx::FloatPoint inner_corner = Gfx::FloatPoint((opposite_radius.vertical_radius - device_pixel_width.value()) * (1 - AK::cos(AK::Pi<float> * 0.25)), -(opposite_radius.horizontal_radius - device_pixel_width.value()) * AK::sin<float>(AK::Pi<float> * 0.25));
            points.append(Gfx::FloatPoint(rect.top_right().to_type<int>()));
            points.append(Gfx::FloatPoint(rect.top_right().to_type<int>()) + inner_corner);
        } else {
            points.append(Gfx::FloatPoint(rect.top_right().to_type<int>()) + border_corner_gap_2);
        }
        points.append(Gfx::FloatPoint(rect.top_left().to_type<int>()) + corner_offset_2);
        points.append(Gfx::FloatPoint(rect.top_left().to_type<int>()));
        draw_border(points, left, right);
        break;
    }
    }
}

RefPtr<Gfx::Bitmap> get_cached_corner_bitmap(DevicePixelSize corners_size)
{
    auto allocate_mask_bitmap = [&]() -> RefPtr<Gfx::Bitmap> {
        auto bitmap = Gfx::Bitmap::create(Gfx::BitmapFormat::BGRA8888, corners_size.to_type<int>());
        if (!bitmap.is_error())
            return bitmap.release_value();
        return nullptr;
    };
    // FIXME: Allocate per page?
    static thread_local auto corner_bitmap = allocate_mask_bitmap();
    // Only reallocate the corner bitmap is the existing one is too small.
    // (should mean no more allocations after the first paint -- amortised zero allocations :^))
    if (corner_bitmap && corner_bitmap->rect().size().contains(corners_size.to_type<int>())) {
        Gfx::Painter painter { *corner_bitmap };
        painter.clear_rect({ { 0, 0 }, corners_size }, Gfx::Color());
    } else {
        corner_bitmap = allocate_mask_bitmap();
        if (!corner_bitmap) {
            dbgln("Failed to allocate corner bitmap with size {}", corners_size);
            return nullptr;
        }
    }
    return corner_bitmap;
}

void paint_all_borders(PaintContext& context, CSSPixelRect const& bordered_rect, BorderRadiiData const& border_radii_data, BordersData const& borders_data)
{
    if (borders_data.top.width <= 0 && borders_data.right.width <= 0 && borders_data.left.width <= 0 && borders_data.bottom.width <= 0)
        return;

    auto border_rect = context.rounded_device_rect(bordered_rect);

    auto top_left = border_radii_data.top_left.as_corner(context);
    auto top_right = border_radii_data.top_right.as_corner(context);
    auto bottom_right = border_radii_data.bottom_right.as_corner(context);
    auto bottom_left = border_radii_data.bottom_left.as_corner(context);

    // Disable border radii if the corresponding borders don't exist:
    if (borders_data.bottom.width <= 0 && borders_data.left.width <= 0)
        bottom_left = { 0, 0 };
    if (borders_data.bottom.width <= 0 && borders_data.right.width <= 0)
        bottom_right = { 0, 0 };
    if (borders_data.top.width <= 0 && borders_data.left.width <= 0)
        top_left = { 0, 0 };
    if (borders_data.top.width <= 0 && borders_data.right.width <= 0)
        top_right = { 0, 0 };

    DevicePixelRect top_border_rect = {
        border_rect.x() + top_left.horizontal_radius,
        border_rect.y(),
        border_rect.width() - top_left.horizontal_radius - top_right.horizontal_radius,
        context.enclosing_device_pixels(borders_data.top.width)
    };
    DevicePixelRect right_border_rect = {
        border_rect.x() + (border_rect.width() - context.enclosing_device_pixels(borders_data.right.width)),
        border_rect.y() + top_right.vertical_radius,
        context.enclosing_device_pixels(borders_data.right.width),
        border_rect.height() - top_right.vertical_radius - bottom_right.vertical_radius
    };
    DevicePixelRect bottom_border_rect = {
        border_rect.x() + bottom_left.horizontal_radius,
        border_rect.y() + (border_rect.height() - context.enclosing_device_pixels(borders_data.bottom.width)),
        border_rect.width() - bottom_left.horizontal_radius - bottom_right.horizontal_radius,
        context.enclosing_device_pixels(borders_data.bottom.width)
    };
    DevicePixelRect left_border_rect = {
        border_rect.x(),
        border_rect.y() + top_left.vertical_radius,
        context.enclosing_device_pixels(borders_data.left.width),
        border_rect.height() - top_left.vertical_radius - bottom_left.vertical_radius
    };

    // Avoid overlapping pixels on the edges, in the easy 45 degree corners case:
    if (!top_left && top_border_rect.height() == left_border_rect.width())
        top_border_rect.inflate(0, 0, 0, 1);
    if (!top_right && top_border_rect.height() == right_border_rect.width())
        top_border_rect.inflate(0, 1, 0, 0);
    if (!bottom_left && bottom_border_rect.height() == left_border_rect.width())
        bottom_border_rect.inflate(0, 0, 0, 1);
    if (!bottom_right && bottom_border_rect.height() == right_border_rect.width())
        bottom_border_rect.inflate(0, 1, 0, 0);

    auto border_color_no_alpha = borders_data.top.color;
    border_color_no_alpha.set_alpha(255);

    // Paint the strait line part of the border:
    paint_border(context, BorderEdge::Top, top_border_rect, top_left, top_right, borders_data);
    paint_border(context, BorderEdge::Right, right_border_rect, top_right, bottom_right, borders_data);
    paint_border(context, BorderEdge::Bottom, bottom_border_rect, bottom_right, bottom_left, borders_data);
    paint_border(context, BorderEdge::Left, left_border_rect, bottom_left, top_left, borders_data);
}

}
