// Phase 7: Blender-style scene editor.
// A single navigable 3D viewport that continuously (coarsely) raytraces the scene,
// click-to-select with an orange outline + orbit overlay, an Add menu, a contextual
// properties panel, and a toggle to look through the render camera.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include "stb/stb_image_write.h"

#include "scene.h"
#include "sphere.h"
#include "disk.h"

namespace {

constexpr float PI = 3.14159265358979323846f;

// Selection sentinels: -1 nothing, -2 the camera object; >= 0 indexes a body.
constexpr int SEL_NONE = -1;
constexpr int SEL_CAMERA = -2;

// ---- Editor (orbit) camera -------------------------------------------------
// Orbits a target point. Default looks straight down (top view).
struct OrbitCam {
    Vec3 target{0, 0, 0};
    float yaw = 0.0f;
    float pitch = -PI * 0.5f;  // straight down
    float distance = 24.0f;
    float fov = 50.0f;
};

// Camera-look direction (eye -> target) from yaw/pitch.
Vec3 cam_forward(float yaw, float pitch) {
    return Vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                std::cos(pitch) * std::cos(yaw));
}
// Horizontal right vector (depends on yaw only, so it's stable at the poles).
Vec3 cam_right(float yaw) { return Vec3(std::cos(yaw), 0.0f, -std::sin(yaw)); }

Camera build_editor_camera(const OrbitCam& c, float aspect) {
    Vec3 fwd = cam_forward(c.yaw, c.pitch);
    Vec3 up = cross(fwd, cam_right(c.yaw));  // world-up-aligned (+Y for level views)
    Vec3 eye = c.target - fwd * c.distance;
    return Camera(eye, c.target, up, c.fov, aspect);
}

// Aim the edit camera down a world axis (X/Y/Z keys: align the view with an axis).
void align_view(OrbitCam& c, Vec3 forward_dir) {
    Vec3 f = normalize(forward_dir);
    c.pitch = std::asin(std::clamp(f.y, -1.0f, 1.0f));
    c.yaw = std::atan2(f.x, f.z);
}

// Orthonormal basis (u, v) spanning the plane perpendicular to `n`.
void basis_from_normal(Vec3 n, Vec3& u, Vec3& v) {
    n = normalize(n);
    Vec3 ref = (std::fabs(n.x) > 0.9f) ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
    u = normalize(cross(ref, n));
    v = cross(n, u);
}

// ---- Editor state ----------------------------------------------------------
// Blender-style modal transform: press G/R/S, move the mouse, click/Enter to
// confirm, RMB/Esc to cancel.
enum class XMode { None, Grab, Rotate, Scale };

struct Editor {
    Scene scene;
    int selected = -1;
    float time = 0.0f;
    bool playing = false;
    float play_speed = 1.0f;
    OrbitCam cam;
    bool look_through_camera = false;
    bool show_orbits = true;  // orbit-path overlays in the edit view (hidden through camera)
    ShadeMode shade_mode = ShadeMode::Lit;  // viewport display mode (export is always Lit)

    GLuint tex = 0;
    std::vector<uint32_t> pixels;

    // Viewport re-render gating: only raytrace when something can have changed,
    // so an idle editor doesn't burn a core. `vp_hovered` is last frame's state;
    // `redraw_frames` keeps rendering a few frames past activity so edits settle.
    bool vp_hovered = false;
    int redraw_frames = 2;
    int last_rw = 0, last_rh = 0;

    // Neutral-light preview thumbnails (material in the Object tab, sky in World).
    GLuint mat_tex = 0, bg_tex = 0;
    std::vector<uint32_t> mat_px, bg_px;

    // Active modal transform + the snapshot taken when it began (for cancel).
    XMode xmode = XMode::None;
    ImVec2 x_start_mouse{0, 0};
    Vec3 x_center, x_normal, x_spin;
    float x_radius = 0, x_inner = 0, x_outer = 0;
};

Scene make_default_scene() {
    Scene s;
    auto sun_shape = std::make_shared<Sphere>(Vec3(0, 0, 0), 1.5f,
                                              Material{Vec3(1.0f, 0.95f, 0.6f), true});
    sun_shape->name = "Sun";
    Body sun{sun_shape};

    Material planet_mat{Vec3(0.2f, 0.5f, 1.0f), false};
    planet_mat.pattern = 2;  // mottled, so spin is visible
    planet_mat.detail = Vec3(0.5f, 0.7f, 0.4f);
    auto planet_shape = std::make_shared<Sphere>(Vec3(0, 0, 0), 0.6f, planet_mat);
    planet_shape->name = "Planet";
    Body planet{planet_shape};
    planet.orbit.active = true;
    planet.orbit.parent = 0;
    planet.orbit.radius = 6.0f;
    planet.orbit.period = 12.0f;
    planet.spin.period = 4.0f;  // one rotation every 4s

    s.bodies = {sun, planet};
    s.light_dir = Vec3(-1, -1, -0.5f);
    s.cam.position = Vec3(0, 6, 14);
    s.cam.fov = 50.0f;
    s.cam.look = CamLook::Target;
    s.cam.target = 0;  // track the Sun
    generate_palette(s.post);  // Stage 3: a starting harmonic palette (enabled by default)
    return s;
}

Camera active_camera(const Editor& ed, float aspect) {
    if (ed.look_through_camera)
        return scene_camera(ed.scene, ed.time, aspect);
    return build_editor_camera(ed.cam, aspect);
}

float active_fov(const Editor& ed) {
    return ed.look_through_camera ? ed.scene.cam.fov : ed.cam.fov;
}

// Pick the nearest body hit by a ray; returns body index or -1.
int pick_body(const Editor& ed, const Ray& ray) {
    World world = world_at_time(ed.scene, ed.time);
    int best = -1;
    float closest = 1e30f;
    HitRecord rec;
    for (int i = 0; i < static_cast<int>(world.size()); ++i) {
        if (world[i]->hit(ray, 1e-3f, closest, rec)) {
            closest = rec.t;
            best = i;
        }
    }
    return best;
}

// ---- GL texture for the viewport image ------------------------------------
void upload_rgba(GLuint& tex, int w, int h, const std::vector<uint32_t>& px) {
    if (tex == 0)
        glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
}

void upload_texture(Editor& ed, int w, int h) {
    upload_rgba(ed.tex, w, h, ed.pixels);
}

// ---- Viewport overlays -----------------------------------------------------
const ImU32 COL_ORBIT = IM_COL32(110, 110, 140, 150);
const ImU32 COL_ORBIT_SEL = IM_COL32(255, 150, 40, 220);
const ImU32 COL_OUTLINE = IM_COL32(255, 150, 40, 255);

// Fixed-layout panels: user can't move/resize/collapse them.
const ImGuiWindowFlags PANEL_FLAGS =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

struct ScreenMap {
    Camera cam;
    ImVec2 origin;  // top-left of the image rect, in screen pixels
    ImVec2 size;    // image rect size in pixels
    bool to_screen(Vec3 world, ImVec2& out) const {
        Projection pr = cam.project(world);
        if (pr.depth <= 0.0f) return false;  // behind the camera
        out = ImVec2(origin.x + pr.s * size.x, origin.y + (1.0f - pr.t) * size.y);
        return true;
    }
};

void draw_orbit(ImDrawList* dl, const ScreenMap& map, const Scene& scene, int i,
                float time, ImU32 col) {
    constexpr int N = 64;
    ImVec2 pts[N];
    int n = 0;
    bool any_behind = false;
    for (int k = 0; k < N; ++k) {
        float ang = (2.0f * PI * k) / N;
        if (!map.to_screen(orbit_ring_point(scene, i, time, ang), pts[n])) {
            any_behind = true;
            continue;
        }
        ++n;
    }
    if (n >= 2)
        dl->AddPolyline(pts, n, col, any_behind ? 0 : ImDrawFlags_Closed, 1.5f);
}

void draw_selection_outline(ImDrawList* dl, const ScreenMap& map,
                            const Body& body, float fov_deg) {
    Vec3 center = body.shape->center;  // already posed via the picked world copy
    ImVec2 c;
    Projection pr = map.cam.project(center);
    if (pr.depth <= 0.0f || !map.to_screen(center, c)) return;

    float half_fov = fov_deg * 0.5f * PI / 180.0f;
    float px_per_world = (map.size.y * 0.5f) / (pr.depth * std::tan(half_fov));

    if (auto s = std::dynamic_pointer_cast<Sphere>(body.shape)) {
        dl->AddCircle(c, s->radius * px_per_world, COL_OUTLINE, 48, 2.0f);
    } else if (auto d = std::dynamic_pointer_cast<Disk>(body.shape)) {
        // Project the outer and inner rims as polylines.
        Vec3 bu, bv;
        basis_from_normal(d->normal, bu, bv);
        for (float r : {d->outer_radius, d->inner_radius}) {
            if (r <= 0.0f) continue;
            constexpr int N = 48;
            ImVec2 pts[N];
            int n = 0;
            for (int k = 0; k < N; ++k) {
                float a = (2.0f * PI * k) / N;
                Vec3 wp = center + (bu * std::cos(a) + bv * std::sin(a)) * r;
                if (map.to_screen(wp, pts[n])) ++n;
            }
            if (n >= 2) dl->AddPolyline(pts, n, COL_OUTLINE, ImDrawFlags_Closed, 2.0f);
        }
    }
}

const ImU32 COL_CAMERA = IM_COL32(220, 220, 220, 200);

// Screen position of the camera's eye at the current time. False if behind us.
bool camera_screen_pos(const ScreenMap& map, const Scene& scene, float t, ImVec2& out) {
    return map.to_screen(camera_eye(scene, t), out);
}

// A little frustum gizmo at the camera's posed position/orientation.
void draw_camera_gizmo(ImDrawList* dl, const ScreenMap& map, const Scene& scene,
                       float t, ImU32 col) {
    Camera c = scene_camera(scene, t, 1.0f);  // aspect-independent for eye/basis
    Vec3 eye = c.eye(), f = c.forward(), r = c.right(), u = c.up();
    const float depth = 0.9f, hw = 0.6f, hh = 0.45f;
    Vec3 ctr = eye + f * depth;
    Vec3 corners[4] = {ctr + r * hw + u * hh, ctr - r * hw + u * hh,
                       ctr - r * hw - u * hh, ctr + r * hw - u * hh};
    ImVec2 a, p[4];
    if (!map.to_screen(eye, a)) return;
    for (int i = 0; i < 4; ++i)
        if (!map.to_screen(corners[i], p[i])) return;
    for (int i = 0; i < 4; ++i) {
        dl->AddLine(a, p[i], col, 1.5f);
        dl->AddLine(p[i], p[(i + 1) % 4], col, 1.5f);
    }
    dl->AddCircleFilled(a, 3.0f, col);
}

// The camera's own orbit path (mirrors draw_orbit, which is body-indexed).
void draw_camera_orbit(ImDrawList* dl, const ScreenMap& map, const Scene& scene,
                       float t, ImU32 col) {
    const Orbit& o = scene.cam.orbit;
    Vec3 base(0, 0, 0);
    if (o.parent >= 0 && o.parent < static_cast<int>(scene.bodies.size()))
        base = body_world_pos(scene, o.parent, t);
    Vec3 bu, bv;
    basis_from_normal(o.normal, bu, bv);
    constexpr int N = 64;
    ImVec2 pts[N];
    int n = 0;
    bool any_behind = false;
    for (int k = 0; k < N; ++k) {
        float ang = (2.0f * PI * k) / N;
        Vec3 wp = base + (bu * std::cos(ang) + bv * std::sin(ang)) * o.radius;
        if (map.to_screen(wp, pts[n])) ++n; else any_behind = true;
    }
    if (n >= 2)
        dl->AddPolyline(pts, n, col, any_behind ? 0 : ImDrawFlags_Closed, 1.5f);
}

// A small crosshair-in-a-ring marker at a world point (scene origin / view pivot).
void draw_marker(ImDrawList* dl, const ScreenMap& map, Vec3 p, ImU32 col,
                 const char* label) {
    ImVec2 s;
    if (!map.to_screen(p, s)) return;
    const float r = 6.0f;
    dl->AddLine(ImVec2(s.x - r, s.y), ImVec2(s.x + r, s.y), col, 1.5f);
    dl->AddLine(ImVec2(s.x, s.y - r), ImVec2(s.x, s.y + r), col, 1.5f);
    dl->AddCircle(s, r, col, 16, 1.5f);
    if (label) dl->AddText(ImVec2(s.x + r + 2, s.y - 6), col, label);
}

// Corner axis indicator: short lines along the projected world X/Y/Z directions.
void draw_axis_gizmo(ImDrawList* dl, const Camera& cam, ImVec2 corner) {
    Vec3 r = cam.right(), u = cam.up();
    const float len = 24.0f;
    struct Axis { Vec3 dir; ImU32 col; const char* name; };
    const Axis axes[3] = {
        {Vec3(1, 0, 0), IM_COL32(232, 86, 86, 255), "X"},
        {Vec3(0, 1, 0), IM_COL32(124, 200, 96, 255), "Y"},
        {Vec3(0, 0, 1), IM_COL32(92, 142, 236, 255), "Z"},
    };
    for (const Axis& ax : axes) {
        ImVec2 tip(corner.x + dot(ax.dir, r) * len, corner.y - dot(ax.dir, u) * len);
        dl->AddLine(corner, tip, ax.col, 2.0f);
        dl->AddText(ImVec2(tip.x - 3, tip.y - 7), ax.col, ax.name);
    }
}

// ---- Properties panel (contextual) ----------------------------------------

// Shared Orbit editor used by both bodies and the camera. `exclude` is a body
// index to omit from the parent picker (a body can't orbit itself); -1 excludes none.
void orbit_controls(Orbit& orbit, const std::vector<Body>& bodies, int exclude) {
    // A radius-0 active orbit is a pure attachment (e.g. a ring tracking its planet):
    // it has no orbit geometry, so Radius/Period/Phase/Normal are meaningless.
    const bool attached = orbit.active && orbit.radius == 0.0f;
    ImGui::SeparatorText(attached ? "Attachment" : "Orbit");
    ImGui::Checkbox("Orbiting", &orbit.active);
    if (!orbit.active) return;
    const char* cur = (orbit.parent < 0 || orbit.parent >= static_cast<int>(bodies.size()))
                          ? "(origin)"
                          : bodies[orbit.parent].shape->name.c_str();
    if (ImGui::BeginCombo(attached ? "Attached to" : "Parent", cur)) {
        if (ImGui::Selectable("(origin)", orbit.parent < 0)) orbit.parent = -1;
        for (int i = 0; i < static_cast<int>(bodies.size()); ++i) {
            if (i == exclude) continue;
            if (ImGui::Selectable(bodies[i].shape->name.c_str(), orbit.parent == i))
                orbit.parent = i;
        }
        ImGui::EndCombo();
    }
    if (attached) {
        ImGui::TextDisabled("Tracks parent's position (no orbit).");
        if (ImGui::SmallButton("Make it orbit")) orbit.radius = 5.0f;
        return;
    }
    ImGui::DragFloat("Orbit Radius", &orbit.radius, 0.1f, 0.0f, 1000.0f);
    ImGui::DragFloat("Period (s)", &orbit.period, 0.1f, 0.01f, 100000.0f);
    ImGui::SliderAngle("Phase", &orbit.phase);
    ImGui::DragFloat3("Plane Normal", &orbit.normal.x, 0.05f);
}

void draw_camera_properties(Editor& ed) {
    SceneCamera& c = ed.scene.cam;
    ImGui::TextUnformatted("Camera");
    ImGui::SeparatorText("Transform");
    if (!c.orbit.active) ImGui::DragFloat3("Position", &c.position.x, 0.1f);
    ImGui::SliderFloat("FOV", &c.fov, 10.0f, 120.0f);
    ImGui::DragFloat3("Up", &c.vup.x, 0.05f);

    ImGui::SeparatorText("Look");
    const char* modes[] = {"Direction", "Target", "Spin"};
    int li = static_cast<int>(c.look);
    if (ImGui::Combo("Mode", &li, modes, 3)) c.look = static_cast<CamLook>(li);
    if (c.look == CamLook::Direction) {
        ImGui::DragFloat3("Direction", &c.direction.x, 0.05f);
    } else if (c.look == CamLook::Target) {
        const char* cur = (c.target < 0 || c.target >= static_cast<int>(ed.scene.bodies.size()))
                              ? "(origin)"
                              : ed.scene.bodies[c.target].shape->name.c_str();
        if (ImGui::BeginCombo("Target", cur)) {
            if (ImGui::Selectable("(origin)", c.target < 0)) c.target = -1;
            for (int i = 0; i < static_cast<int>(ed.scene.bodies.size()); ++i)
                if (ImGui::Selectable(ed.scene.bodies[i].shape->name.c_str(), c.target == i))
                    c.target = i;
            ImGui::EndCombo();
        }
    } else {  // Spin
        ImGui::DragFloat("Spin Period (s)", &c.spin_period, 0.1f);
        ImGui::SliderAngle("Spin Phase", &c.spin_phase);
    }

    orbit_controls(c.orbit, ed.scene.bodies, -1);
}
// Editor for a list of color stops (ring gradient / texture ramp). `add_color`
// is the color a freshly added stop gets.
void ramp_editor(std::vector<ColorStop>& ramp, Vec3 add_color) {
    int remove = -1;
    for (int k = 0; k < static_cast<int>(ramp.size()); ++k) {
        ImGui::PushID(k);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("Pos", &ramp[k].pos, 0.0f, 1.0f);
        ImGui::SameLine();
        ImGui::ColorEdit3("##col", &ramp[k].color.x, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) remove = k;
        ImGui::PopID();
    }
    if (remove >= 0) ramp.erase(ramp.begin() + remove);
    if (ImGui::SmallButton("Add stop"))
        ramp.push_back({ramp.empty() ? 0.0f : 1.0f, add_color});
}

// Object tab: the selected body (or camera), else a hint.
void draw_object_tab(Editor& ed) {
    if (ed.selected == SEL_CAMERA) {
        draw_camera_properties(ed);
        return;
    }
    if (ed.selected < 0 || ed.selected >= static_cast<int>(ed.scene.bodies.size())) {
        ImGui::TextWrapped("Nothing selected. Click an object in the viewport, "
                           "or use the Add menu to create one.");
        return;
    }

    Body& body = ed.scene.bodies[ed.selected];
    auto& obj = body.shape;

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", obj->name.c_str());
    if (ImGui::InputText("Name", buf, sizeof(buf))) obj->name = buf;

    const bool orbiting = body.orbit.active;

    ImGui::SeparatorText("Transform");
    if (auto s = std::dynamic_pointer_cast<Sphere>(obj)) {
        ImGui::TextUnformatted("Sphere");
        if (!orbiting) ImGui::DragFloat3("Position", &s->center.x, 0.1f);
        ImGui::SliderFloat("Radius", &s->radius, 0.05f, 20.0f);
        ImGui::SeparatorText("Material");
        Material& m = s->material;
        ImGui::ColorEdit3("Albedo", &m.albedo.x);
        ImGui::Checkbox("Emissive", &m.emissive);
        bool textured = m.pattern != 0;
        if (ImGui::Checkbox("Textured", &textured)) m.pattern = textured ? 1 : 0;
        if (m.pattern != 0) {
            ImGui::SliderFloat("Noise scale", &m.noise_scale, 0.5f, 16.0f);
            ImGui::SliderInt("Octaves", &m.noise_octaves, 1, 8);
            ImGui::SliderFloat("Band strength", &m.band_strength, 0.0f, 1.0f);
            ImGui::SliderFloat("Band freq", &m.band_freq, 1.0f, 30.0f);
            ImGui::SliderFloat("Warp", &m.warp, 0.0f, 2.0f);
            ImGui::SeparatorText("Surface colors");
            ramp_editor(m.tex_ramp, m.albedo);
            if (m.tex_ramp.empty()) ImGui::ColorEdit3("Detail", &m.detail.x);
        }
        ImGui::SeparatorText("Atmosphere");
        ImGui::Checkbox("Has atmosphere", &m.atmosphere.enabled);
        if (m.atmosphere.enabled) {
            ImGui::ColorEdit3("Day color", &m.atmosphere.color.x);
            ImGui::ColorEdit3("Sunset color", &m.atmosphere.sunset.x);
            ImGui::SliderFloat("Thickness", &m.atmosphere.thickness, 0.0f, 1.0f);
            ImGui::SliderFloat("Intensity", &m.atmosphere.intensity, 0.0f, 3.0f);
        }
        ImGui::SeparatorText("Preview (neutral light)");
        constexpr int PW = 140;
        render_material_preview(m, PW, PW, ed.mat_px);
        upload_rgba(ed.mat_tex, PW, PW, ed.mat_px);
        ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(ed.mat_tex)),
                     ImVec2(PW, PW));
    } else if (auto d = std::dynamic_pointer_cast<Disk>(obj)) {
        ImGui::TextUnformatted("Ring");
        if (!orbiting) ImGui::DragFloat3("Position", &d->center.x, 0.1f);
        ImGui::DragFloat3("Normal", &d->normal.x, 0.05f);
        ImGui::SliderFloat("Inner Radius", &d->inner_radius, 0.0f, 20.0f);
        ImGui::SliderFloat("Outer Radius", &d->outer_radius, 0.0f, 20.0f);
        ImGui::SeparatorText("Material");
        ImGui::ColorEdit3("Albedo", &d->material.albedo.x);
        ImGui::Checkbox("Emissive", &d->material.emissive);

        ImGui::SeparatorText("Color ramp (inner -> outer)");
        if (d->material.ring_ramp.empty()) ImGui::TextDisabled("Solid (uses Albedo)");
        ramp_editor(d->material.ring_ramp, d->material.albedo);
    }

    orbit_controls(body.orbit, ed.scene.bodies, ed.selected);

    ImGui::SeparatorText("Spin (visible once textured)");
    ImGui::DragFloat3("Spin Axis", &body.spin.axis.x, 0.05f);
    ImGui::DragFloat("Spin Period (s)", &body.spin.period, 0.1f);
}

// World tab: scene-global lighting + background starfield.
void draw_world_tab(Editor& ed) {
    ImGui::SeparatorText("Lighting");
    ImGui::TextWrapped("Suns (emissive bodies) light the scene in Lit mode.");
    ImGui::DragFloat3("Fill light dir", &ed.scene.light_dir.x, 0.05f);
    ImGui::Checkbox("Include fill in Lit", &ed.scene.fill_light);
    ImGui::Checkbox("Sun distance falloff", &ed.scene.light_falloff);

    Background& bg = ed.scene.background;
    ImGui::SeparatorText("Background (starfield)");
    ImGui::ColorEdit3("Sky", &bg.sky.x);
    ImGui::SliderFloat("Density", &bg.density, 0.0f, 1.0f);
    ImGui::SliderFloat("Brightness", &bg.brightness, 0.0f, 4.0f);
    ImGui::SliderFloat("Star size", &bg.size, 0.01f, 0.5f);
    ImGui::ColorEdit3("Star tint", &bg.tint.x);
    ImGui::SliderFloat("Color variation", &bg.color_variation, 0.0f, 1.0f);
    ImGui::InputInt("Seed", &bg.seed);

    ImGui::SeparatorText("Density regions");
    ImGui::SliderFloat("Region scale", &bg.region_scale, 0.2f, 8.0f);
    ImGui::SliderFloat("Region strength", &bg.region_strength, 0.0f, 1.0f);
    ImGui::SliderFloat("Region glow", &bg.region_glow, 0.0f, 0.3f);

    ImGui::SeparatorText("Preview");
    constexpr int PW = 224, PH = 126;
    render_background_preview(bg, PW, PH, ed.bg_px);
    upload_rgba(ed.bg_tex, PW, PH, ed.bg_px);
    ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(ed.bg_tex)), ImVec2(PW, PH));
}

// Stylize tab: palette post-process.
void draw_stylize_tab(Editor& ed) {
    PostProcess& pp = ed.scene.post;
    ImGui::Checkbox("Enabled", &pp.enabled);

    ImGui::SeparatorText("Generate");
    const char* anchors[] = {"None (random)", "One color", "Two colors"};
    ImGui::Combo("Anchors", &pp.anchor_count, anchors, 3);
    if (pp.anchor_count >= 1) ImGui::ColorEdit3("Base A", &pp.base_a.x);
    if (pp.anchor_count >= 2) ImGui::ColorEdit3("Base B", &pp.base_b.x);
    ImGui::SliderInt("Palette size", &pp.palette_size, 2, 32);
    ImGui::SliderFloat("Randomness", &pp.randomness, 0.0f, 1.0f);
    ImGui::InputInt("Palette seed", &pp.palette_seed);
    if (ImGui::Button("Generate palette")) generate_palette(pp);
    ImGui::SameLine();
    if (ImGui::Button("Randomize")) { pp.palette_seed++; generate_palette(pp); }

    int rm = -1;
    for (int k = 0; k < static_cast<int>(pp.palette.size()); ++k) {
        ImGui::PushID(k);
        ImGui::ColorEdit3("##sw", &pp.palette[k].x, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) rm = k;
        if ((k % 6) != 5 && k + 1 < static_cast<int>(pp.palette.size()))
            ImGui::SameLine();
        ImGui::PopID();
    }
    if (rm >= 0) pp.palette.erase(pp.palette.begin() + rm);
    if (ImGui::SmallButton("Add swatch")) pp.palette.push_back(Vec3(1, 1, 1));

    ImGui::SliderFloat("Blur radius", &pp.blur_radius, 0.0f, 5.0f);
    const char* dither[] = {"None", "Ordered (Bayer)", "Random"};
    int dm = static_cast<int>(pp.dither);
    if (ImGui::Combo("Dither", &dm, dither, 3)) pp.dither = static_cast<DitherMode>(dm);
    ImGui::SliderInt("Iterations", &pp.iterations, 1, 4);
}

void draw_properties(Editor& ed) {
    ImGui::Begin("Properties", nullptr, PANEL_FLAGS);
    if (ImGui::BeginTabBar("PropTabs")) {
        if (ImGui::BeginTabItem("Object")) { draw_object_tab(ed); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("World")) { draw_world_tab(ed); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Stylize")) { draw_stylize_tab(ed); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---- Timeline / scene strip ------------------------------------------------
void draw_timeline(Editor& ed) {
    ImGui::Begin("Timeline", nullptr, PANEL_FLAGS);

    if (ImGui::Button(ed.playing ? "Pause" : "Play")) ed.playing = !ed.playing;
    ImGui::SameLine();
    if (ImGui::Button("Reset")) ed.time = 0.0f;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragFloat("Speed", &ed.play_speed, 0.05f, 0.0f, 100.0f);
    ImGui::SameLine();
    ImGui::Text("Output: %dx%d", ed.scene.width, ed.scene.height);
    ImGui::SameLine();
    if (ImGui::Button("Export PNG")) {
        std::vector<uint32_t> full;  // full-res render from the render camera
        render_scene(ed.scene, ed.time, full);
        stbi_write_png("render.png", ed.scene.width, ed.scene.height, 4,
                       full.data(), ed.scene.width * 4);
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::DragFloat("##time", &ed.time, 0.05f, 0.0f, 0.0f, "Time: %.2f s");
    ImGui::End();
}

void add_sphere(Editor& ed) {
    auto s = std::make_shared<Sphere>(ed.cam.target, 1.0f,
                                      Material{Vec3(0.8f, 0.8f, 0.8f), false});
    s->name = "Sphere";
    ed.scene.bodies.push_back(Body{s});
    ed.selected = static_cast<int>(ed.scene.bodies.size()) - 1;
}

void add_ring(Editor& ed) {
    auto ring = std::make_shared<Disk>(ed.cam.target, Vec3(0, 1, 0), 1.5f, 3.0f,
                                       Material{Vec3(0.7f, 0.6f, 0.5f), false});
    ring->name = "Ring";
    Body body{ring};

    // If a sphere is selected, size the ring to it (1.2x / 1.4x its radius), sit it
    // in the planet's equatorial plane, and attach it via a radius-0 orbit so it
    // tracks the planet wherever it goes.
    if (ed.selected >= 0 && ed.selected < static_cast<int>(ed.scene.bodies.size())) {
        if (auto planet = std::dynamic_pointer_cast<Sphere>(ed.scene.bodies[ed.selected].shape)) {
            ring->inner_radius = planet->radius * 1.2f;
            ring->outer_radius = planet->radius * 1.4f;
            ring->normal = planet->spin_axis;
            ring->center = planet->center;
            body.orbit.active = true;
            body.orbit.parent = ed.selected;
            body.orbit.radius = 0.0f;
            body.orbit.normal = planet->spin_axis;
        }
    }
    ed.scene.bodies.push_back(body);
    ed.selected = static_cast<int>(ed.scene.bodies.size()) - 1;
}

void delete_selected(Editor& ed) {
    if (ed.selected < 0 || ed.selected >= static_cast<int>(ed.scene.bodies.size())) return;
    ed.scene.bodies.erase(ed.scene.bodies.begin() + ed.selected);
    ed.selected = SEL_NONE;
}

void draw_menu_bar(Editor& ed) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Add")) {
            if (ImGui::MenuItem("Sphere")) add_sphere(ed);
            if (ImGui::MenuItem("Ring")) add_ring(ed);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Object")) {
            const bool has_sel = ed.selected >= 0;
            if (ImGui::MenuItem("Delete", "Del", false, has_sel)) delete_selected(ed);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Look through camera", "0", &ed.look_through_camera);
            ImGui::MenuItem("Show orbits", nullptr, &ed.show_orbits);
            if (ImGui::BeginMenu("Display mode")) {
                int m = static_cast<int>(ed.shade_mode);
                if (ImGui::RadioButton("Direction (flat directional)", &m, 0))
                    ed.shade_mode = ShadeMode::Direction;
                if (ImGui::RadioButton("In-between (sun dir, no shadows)", &m, 1))
                    ed.shade_mode = ShadeMode::InBetween;
                if (ImGui::RadioButton("Lit (suns + shadows, = export)", &m, 2))
                    ed.shade_mode = ShadeMode::Lit;
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Top")) { ed.cam.pitch = -PI * 0.5f; ed.cam.yaw = 0; }
            if (ImGui::MenuItem("Front")) { ed.cam.pitch = 0; ed.cam.yaw = 0; }
            if (ImGui::MenuItem("Side")) { ed.cam.pitch = 0; ed.cam.yaw = PI * 0.5f; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save")) save_scene(ed.scene, "scene.json");
            if (ImGui::MenuItem("Load")) {
                Scene loaded;
                if (load_scene(loaded, "scene.json")) {
                    ed.scene = loaded;
                    ed.selected = -1;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

// Right-drag orbits the view around the pivot; wheel zooms. (Left drag moves the
// selected object instead — handled in the viewport.)
void handle_navigation(Editor& ed, bool hovered) {
    if (!hovered || ed.look_through_camera) return;
    ImGuiIO& io = ImGui::GetIO();

    if (io.MouseWheel != 0.0f)
        ed.cam.distance = std::clamp(ed.cam.distance * std::pow(0.9f, io.MouseWheel),
                                     0.5f, 500.0f);

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ImVec2 d = io.MouseDelta;
        ed.cam.yaw -= d.x * 0.01f;
        ed.cam.pitch = std::clamp(ed.cam.pitch - d.y * 0.01f, -PI * 0.5f, PI * 0.5f);
    }
}

// Snapshot the selected body's editable values so a transform can be cancelled.
void begin_transform(Editor& ed, XMode m) {
    if (ed.selected == SEL_CAMERA) {
        // A free camera can be grabbed; an orbiting one derives its position.
        if (m != XMode::Grab || ed.scene.cam.orbit.active) return;
        ed.xmode = m;
        ed.x_start_mouse = ImGui::GetIO().MousePos;
        ed.x_center = ed.scene.cam.position;
        return;
    }
    if (ed.selected < 0 || ed.selected >= static_cast<int>(ed.scene.bodies.size())) return;
    Body& b = ed.scene.bodies[ed.selected];
    if (m == XMode::Grab && b.orbit.active) return;  // orbiting bodies derive position
    ed.xmode = m;
    ed.x_start_mouse = ImGui::GetIO().MousePos;
    ed.x_center = b.shape->center;
    ed.x_spin = b.spin.axis;
    if (auto s = std::dynamic_pointer_cast<Sphere>(b.shape)) {
        ed.x_radius = s->radius;
    } else if (auto d = std::dynamic_pointer_cast<Disk>(b.shape)) {
        ed.x_inner = d->inner_radius;
        ed.x_outer = d->outer_radius;
        ed.x_normal = d->normal;
    }
}

void cancel_transform(Editor& ed) {
    if (ed.xmode == XMode::None) return;
    if (ed.selected == SEL_CAMERA) {
        ed.scene.cam.position = ed.x_center;
        ed.xmode = XMode::None;
        return;
    }
    Body& b = ed.scene.bodies[ed.selected];
    b.shape->center = ed.x_center;
    b.spin.axis = ed.x_spin;
    if (auto s = std::dynamic_pointer_cast<Sphere>(b.shape)) {
        s->radius = ed.x_radius;
    } else if (auto d = std::dynamic_pointer_cast<Disk>(b.shape)) {
        d->inner_radius = ed.x_inner;
        d->outer_radius = ed.x_outer;
        d->normal = ed.x_normal;
    }
    ed.xmode = XMode::None;
}

// Drive the in-progress transform from the current mouse position.
void update_transform(Editor& ed, const Camera& cam, const ScreenMap& map,
                      float region_h, float fov_deg, Vec3 posed_center) {
    ImVec2 mouse = ImGui::GetIO().MousePos;
    Projection pr = cam.project(posed_center);
    if (pr.depth <= 0.0f) return;
    ImVec2 cscr;
    map.to_screen(posed_center, cscr);

    // Screen-drag -> world translation in the view plane (shared by grab paths).
    float half = fov_deg * 0.5f * PI / 180.0f;
    float wpp = 2.0f * pr.depth * std::tan(half) / region_h;  // world units per pixel
    float dx = (mouse.x - ed.x_start_mouse.x) * wpp;
    float dy = (mouse.y - ed.x_start_mouse.y) * wpp;

    if (ed.selected == SEL_CAMERA) {  // only grab is offered for the camera
        ed.scene.cam.position = ed.x_center + cam.right() * dx - cam.up() * dy;
        return;
    }

    Body& b = ed.scene.bodies[ed.selected];
    if (ed.xmode == XMode::Grab) {
        b.shape->center = ed.x_center + cam.right() * dx - cam.up() * dy;  // screen y is down
    } else if (ed.xmode == XMode::Scale) {
        float d0 = std::hypot(ed.x_start_mouse.x - cscr.x, ed.x_start_mouse.y - cscr.y);
        float d1 = std::hypot(mouse.x - cscr.x, mouse.y - cscr.y);
        float f = (d0 > 1.0f) ? (d1 / d0) : 1.0f;
        if (auto s = std::dynamic_pointer_cast<Sphere>(b.shape)) {
            s->radius = std::max(0.01f, ed.x_radius * f);
        } else if (auto d = std::dynamic_pointer_cast<Disk>(b.shape)) {
            d->inner_radius = std::max(0.0f, ed.x_inner * f);
            d->outer_radius = std::max(0.01f, ed.x_outer * f);
        }
    } else if (ed.xmode == XMode::Rotate) {
        // Trackball: horizontal drag tilts about the view's up axis, vertical drag
        // about its right axis. Works from any view (a flat ring viewed top-down,
        // whose normal points along the old view-axis, now still tilts).
        const float k = 0.01f;  // radians per pixel
        float dxr = (mouse.x - ed.x_start_mouse.x) * k;
        float dyr = (mouse.y - ed.x_start_mouse.y) * k;
        auto trackball = [&](Vec3 v) {
            v = rotate_about(v, cam.up(), dxr);
            return rotate_about(v, cam.right(), dyr);
        };
        if (auto disk = std::dynamic_pointer_cast<Disk>(b.shape)) {
            disk->normal = trackball(ed.x_normal);
        } else {
            b.spin.axis = trackball(ed.x_spin);  // sphere: spin axis (stored)
        }
    }
}

const char* xmode_label(XMode m) {
    switch (m) {
        case XMode::Grab: return "Grab";
        case XMode::Rotate: return "Rotate";
        case XMode::Scale: return "Scale";
        default: return "";
    }
}

void draw_viewport(Editor& ed) {
    ImGui::Begin("Viewport", nullptr, PANEL_FLAGS | ImGuiWindowFlags_NoScrollbar);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int region_w = std::max(16, static_cast<int>(avail.x));
    int region_h = std::max(16, static_cast<int>(avail.y));
    float aspect = static_cast<float>(region_w) / static_cast<float>(region_h);

    // Coarse render resolution (continuous re-render every frame).
    int rw = std::clamp(region_w / 3, 80, 480);
    int rh = std::max(1, static_cast<int>(rw / aspect));
    Camera cam = active_camera(ed, aspect);

    // Re-raytrace only when something can have changed: animation playing, a widget
    // or mouse drag in progress, the cursor over the viewport (where keys act), or a
    // resize. Otherwise reuse the last texture so an idle editor stays at ~0% CPU.
    bool resized = (rw != ed.last_rw || rh != ed.last_rh);
    bool active = ed.playing || resized || ed.vp_hovered ||
                  ImGui::IsAnyItemActive() ||
                  ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                  ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (active) ed.redraw_frames = 3;  // render now + trailing frames to settle edits
    if (ed.redraw_frames > 0) {
        render_view(ed.scene, cam, ed.time, rw, rh, ed.pixels, ed.shade_mode);
        upload_texture(ed, rw, rh);
        ed.last_rw = rw;
        ed.last_rh = rh;
        --ed.redraw_frames;
    }

    ImVec2 img_pos = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(ed.tex)),
                 ImVec2(static_cast<float>(region_w), static_cast<float>(region_h)));
    bool hovered = ImGui::IsItemHovered();
    ed.vp_hovered = hovered;

    ScreenMap map{cam, img_pos, ImVec2(static_cast<float>(region_w),
                                       static_cast<float>(region_h))};
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Orbit overlays (selected brighter), then the selection outline on top.
    // Orbits are an edit-view aid: hidden through the render camera, or by the toggle.
    World posed = world_at_time(ed.scene, ed.time);
    const bool show_orbits = ed.show_orbits && !ed.look_through_camera;
    if (show_orbits)
        for (int i = 0; i < static_cast<int>(ed.scene.bodies.size()); ++i) {
            if (!ed.scene.bodies[i].orbit.active) continue;
            draw_orbit(dl, map, ed.scene, i, ed.time, i == ed.selected ? COL_ORBIT_SEL : COL_ORBIT);
        }
    bool sel_body = ed.selected >= 0 && ed.selected < static_cast<int>(posed.size());
    if (sel_body) {
        Body posed_body = ed.scene.bodies[ed.selected];
        posed_body.shape = posed[ed.selected];  // posed clone carries world center
        draw_selection_outline(dl, map, posed_body, active_fov(ed));
    }

    // Editor-only overlays: render-camera gizmo (+ its orbit), the scene-origin and
    // view-pivot markers, and a corner axis indicator. None of this is raytraced.
    bool sel_cam = ed.selected == SEL_CAMERA && !ed.look_through_camera;
    if (!ed.look_through_camera) {
        if (ed.show_orbits && ed.scene.cam.orbit.active)
            draw_camera_orbit(dl, map, ed.scene, ed.time,
                              sel_cam ? COL_ORBIT_SEL : COL_ORBIT);
        draw_camera_gizmo(dl, map, ed.scene, ed.time, sel_cam ? COL_OUTLINE : COL_CAMERA);
        draw_marker(dl, map, Vec3(0, 0, 0), IM_COL32(210, 200, 90, 200), "O");  // scene center
        draw_marker(dl, map, ed.cam.target, IM_COL32(90, 200, 210, 220), nullptr);  // look-at
        draw_axis_gizmo(dl, cam, ImVec2(img_pos.x + 34, img_pos.y + region_h - 34));
    }
    bool can_xform = sel_body || sel_cam;

    // ---- Keyboard: play, edit-camera views, R/S transforms, delete ---------
    ImGuiIO& io = ImGui::GetIO();
    if (ed.xmode == XMode::None && hovered && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) ed.playing = !ed.playing;
        if (ImGui::IsKeyPressed(ImGuiKey_0) || ImGui::IsKeyPressed(ImGuiKey_Keypad0))
            ed.look_through_camera = !ed.look_through_camera;

        // Edit-camera view controls (distinct from the render camera).
        if (!ed.look_through_camera) {
            if (ImGui::IsKeyPressed(ImGuiKey_X)) align_view(ed.cam, Vec3(-1, 0, 0));
            else if (ImGui::IsKeyPressed(ImGuiKey_Y)) align_view(ed.cam, Vec3(0, -1, 0));
            else if (ImGui::IsKeyPressed(ImGuiKey_Z)) align_view(ed.cam, Vec3(0, 0, -1));
            if (ImGui::IsKeyPressed(ImGuiKey_C)) ed.cam.target = Vec3(0, 0, 0);
            if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
                ed.cam.distance = std::clamp(ed.cam.distance * 0.9f, 0.5f, 500.0f);
            if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
                ed.cam.distance = std::clamp(ed.cam.distance / 0.9f, 0.5f, 500.0f);
        }

        // Rotate/Scale stay modal; Grab is replaced by plain left-drag (below).
        if (can_xform) {
            if (ImGui::IsKeyPressed(ImGuiKey_R)) begin_transform(ed, XMode::Rotate);
            else if (ImGui::IsKeyPressed(ImGuiKey_S)) begin_transform(ed, XMode::Scale);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) delete_selected(ed);
    }

    // ---- Left press: pick a body/camera, recenter the pivot on it, and start a
    // drag-move (a release with no movement is just a selection click). ----------
    if (ed.xmode == XMode::None && hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 m = io.MousePos;
        float s = (m.x - img_pos.x) / region_w;
        float t = 1.0f - (m.y - img_pos.y) / region_h;
        int picked = pick_body(ed, cam.get_ray(s, t));
        ImVec2 cscr;  // the camera isn't geometry — pick it near its gizmo
        if (!ed.look_through_camera &&
            camera_screen_pos(map, ed.scene, ed.time, cscr) &&
            std::hypot(m.x - cscr.x, m.y - cscr.y) < 14.0f)
            picked = SEL_CAMERA;
        ed.selected = picked;
        if (picked == SEL_CAMERA)
            ed.cam.target = camera_eye(ed.scene, ed.time);
        else if (picked >= 0)
            ed.cam.target = posed[picked]->center;  // pivot follows selection
        begin_transform(ed, XMode::Grab);  // no-op for empty/orbiting picks
    }

    bool sel_valid = (ed.selected == SEL_CAMERA) ||
                     (ed.selected >= 0 && ed.selected < static_cast<int>(posed.size()));
    if (ed.xmode != XMode::None) {
        if (!sel_valid) {
            ed.xmode = XMode::None;
        } else {
            Vec3 xform_center = (ed.selected == SEL_CAMERA) ? camera_eye(ed.scene, ed.time)
                                                            : posed[ed.selected]->center;
            update_transform(ed, cam, map, static_cast<float>(region_h), active_fov(ed),
                             xform_center);
            if (ed.xmode != XMode::Grab)  // Grab is a live drag; only label modal R/S
                dl->AddText(ImVec2(img_pos.x + 8, img_pos.y + 6), COL_OUTLINE,
                            xmode_label(ed.xmode));
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                cancel_transform(ed);
            } else if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                       ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                ed.xmode = XMode::None;  // confirm: keep edited values
            }
        }
    }

    if (ed.xmode == XMode::None)
        handle_navigation(ed, hovered);

    if (can_xform && ed.xmode == XMode::None)
        dl->AddText(ImVec2(img_pos.x + 8, img_pos.y + region_h - 22),
                    IM_COL32(210, 210, 210, 150),
                    sel_cam ? "drag move" : "drag move   R rotate   S scale");

    ImGui::End();
}

void layout_and_draw(Editor& ed) {
    ImGuiIO& io = ImGui::GetIO();
    const float menu_h = ImGui::GetFrameHeight();
    const float right_w = 330.0f;
    const float bottom_h = 100.0f;
    const float W = io.DisplaySize.x;
    const float H = io.DisplaySize.y;

    // Advance animation time while playing (orbits/spin pose from `time`).
    if (ed.playing) ed.time += io.DeltaTime * ed.play_speed;

    draw_menu_bar(ed);

    // Three fixed panels: viewport (left), properties (right), timeline (bottom).
    ImGui::SetNextWindowPos(ImVec2(0, menu_h));
    ImGui::SetNextWindowSize(ImVec2(W - right_w, H - menu_h - bottom_h));
    draw_viewport(ed);

    ImGui::SetNextWindowPos(ImVec2(W - right_w, menu_h));
    ImGui::SetNextWindowSize(ImVec2(right_w, H - menu_h));
    draw_properties(ed);

    ImGui::SetNextWindowPos(ImVec2(0, H - bottom_h));
    ImGui::SetNextWindowSize(ImVec2(W - right_w, bottom_h));
    draw_timeline(ed);
}

void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

}  // namespace

int main() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window =
        glfwCreateWindow(1400, 820, "Space Sceneries - Scene Editor", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    Editor ed;
    ed.scene = make_default_scene();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        layout_and_draw(ed);

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    if (ed.tex) glDeleteTextures(1, &ed.tex);
    if (ed.mat_tex) glDeleteTextures(1, &ed.mat_tex);
    if (ed.bg_tex) glDeleteTextures(1, &ed.bg_tex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
