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
    Vec3 up = cross(cam_right(c.yaw), fwd);
    Vec3 eye = c.target - fwd * c.distance;
    return Camera(eye, c.target, up, c.fov, aspect);
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

    GLuint tex = 0;
    std::vector<uint32_t> pixels;

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
    s.cam_lookfrom = Vec3(0, 6, 14);
    s.cam_lookat = Vec3(0, 0, 0);
    s.cam_fov = 50.0f;
    return s;
}

Camera active_camera(const Editor& ed, float aspect) {
    if (ed.look_through_camera)
        return Camera(ed.scene.cam_lookfrom, ed.scene.cam_lookat, ed.scene.cam_vup,
                      ed.scene.cam_fov, aspect);
    return build_editor_camera(ed.cam, aspect);
}

float active_fov(const Editor& ed) {
    return ed.look_through_camera ? ed.scene.cam_fov : ed.cam.fov;
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
void upload_texture(Editor& ed, int w, int h) {
    if (ed.tex == 0)
        glGenTextures(1, &ed.tex);
    glBindTexture(GL_TEXTURE_2D, ed.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 ed.pixels.data());
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

// ---- Properties panel (contextual) ----------------------------------------
void draw_properties(Editor& ed) {
    ImGui::Begin("Properties", nullptr, PANEL_FLAGS);
    if (ed.selected < 0 || ed.selected >= static_cast<int>(ed.scene.bodies.size())) {
        ImGui::TextWrapped("Nothing selected. Click an object in the viewport, "
                           "or use the Add menu to create one.");
        ImGui::End();
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
        ImGui::ColorEdit3("Albedo", &s->material.albedo.x);
        ImGui::Checkbox("Emissive", &s->material.emissive);
        const char* patterns[] = {"Solid", "Stripes", "Mottled"};
        ImGui::Combo("Pattern", &s->material.pattern, patterns, 3);
        if (s->material.pattern != 0)
            ImGui::ColorEdit3("Detail", &s->material.detail.x);
    } else if (auto d = std::dynamic_pointer_cast<Disk>(obj)) {
        ImGui::TextUnformatted("Disk");
        if (!orbiting) ImGui::DragFloat3("Position", &d->center.x, 0.1f);
        ImGui::DragFloat3("Normal", &d->normal.x, 0.05f);
        ImGui::SliderFloat("Inner Radius", &d->inner_radius, 0.0f, 20.0f);
        ImGui::SliderFloat("Outer Radius", &d->outer_radius, 0.0f, 20.0f);
        ImGui::SeparatorText("Material");
        ImGui::ColorEdit3("Albedo", &d->material.albedo.x);
        ImGui::Checkbox("Emissive", &d->material.emissive);
    }

    ImGui::SeparatorText("Orbit");
    ImGui::Checkbox("Orbiting", &body.orbit.active);
    if (body.orbit.active) {
        // Parent picker as a body-name combo (excluding self).
        const char* cur = (body.orbit.parent < 0)
                              ? "(origin)"
                              : ed.scene.bodies[body.orbit.parent].shape->name.c_str();
        if (ImGui::BeginCombo("Parent", cur)) {
            if (ImGui::Selectable("(origin)", body.orbit.parent < 0))
                body.orbit.parent = -1;
            for (int i = 0; i < static_cast<int>(ed.scene.bodies.size()); ++i) {
                if (i == ed.selected) continue;
                if (ImGui::Selectable(ed.scene.bodies[i].shape->name.c_str(),
                                      body.orbit.parent == i))
                    body.orbit.parent = i;
            }
            ImGui::EndCombo();
        }
        ImGui::DragFloat("Orbit Radius", &body.orbit.radius, 0.1f, 0.0f, 1000.0f);
        ImGui::DragFloat("Period (s)", &body.orbit.period, 0.1f, 0.01f, 100000.0f);
        ImGui::SliderAngle("Phase", &body.orbit.phase);
        ImGui::DragFloat3("Plane Normal", &body.orbit.normal.x, 0.05f);
    }

    ImGui::SeparatorText("Spin (visible once textured)");
    ImGui::DragFloat3("Spin Axis", &body.spin.axis.x, 0.05f);
    ImGui::DragFloat("Spin Period (s)", &body.spin.period, 0.1f);

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

void add_disk(Editor& ed) {
    auto d = std::make_shared<Disk>(ed.cam.target, Vec3(0, 1, 0), 1.5f, 3.0f,
                                    Material{Vec3(0.7f, 0.6f, 0.5f), false});
    d->name = "Disk";
    ed.scene.bodies.push_back(Body{d});
    ed.selected = static_cast<int>(ed.scene.bodies.size()) - 1;
}

void draw_menu_bar(Editor& ed) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Add")) {
            if (ImGui::MenuItem("Sphere")) add_sphere(ed);
            if (ImGui::MenuItem("Disk")) add_disk(ed);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Object")) {
            const bool has_sel = ed.selected >= 0;
            if (ImGui::MenuItem("Delete", "X", false, has_sel)) {
                ed.scene.bodies.erase(ed.scene.bodies.begin() + ed.selected);
                ed.selected = -1;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Look through camera", "Numpad 0", &ed.look_through_camera);
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

// Handle MMB orbit / shift+MMB pan / wheel zoom while the viewport image is hovered.
void handle_navigation(Editor& ed, bool hovered) {
    if (!hovered || ed.look_through_camera) return;
    ImGuiIO& io = ImGui::GetIO();

    if (io.MouseWheel != 0.0f)
        ed.cam.distance = std::clamp(ed.cam.distance * std::pow(0.9f, io.MouseWheel),
                                     0.5f, 500.0f);

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 d = io.MouseDelta;
        if (io.KeyShift) {
            Vec3 right = cam_right(ed.cam.yaw);
            Vec3 up = cross(right, cam_forward(ed.cam.yaw, ed.cam.pitch));
            float k = ed.cam.distance * 0.002f;
            ed.cam.target = ed.cam.target - right * (d.x * k) + up * (d.y * k);
        } else {
            ed.cam.yaw -= d.x * 0.01f;
            ed.cam.pitch = std::clamp(ed.cam.pitch - d.y * 0.01f, -PI * 0.5f, PI * 0.5f);
        }
    }
}

// Snapshot the selected body's editable values so a transform can be cancelled.
void begin_transform(Editor& ed, XMode m) {
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
    Body& b = ed.scene.bodies[ed.selected];
    ImVec2 mouse = ImGui::GetIO().MousePos;
    Projection pr = cam.project(posed_center);
    if (pr.depth <= 0.0f) return;
    ImVec2 cscr;
    map.to_screen(posed_center, cscr);

    if (ed.xmode == XMode::Grab) {
        float half = fov_deg * 0.5f * PI / 180.0f;
        float wpp = 2.0f * pr.depth * std::tan(half) / region_h;  // world units per pixel
        float dx = (mouse.x - ed.x_start_mouse.x) * wpp;
        float dy = (mouse.y - ed.x_start_mouse.y) * wpp;
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
        float a0 = std::atan2(ed.x_start_mouse.y - cscr.y, ed.x_start_mouse.x - cscr.x);
        float a1 = std::atan2(mouse.y - cscr.y, mouse.x - cscr.x);
        Vec3 axis = cam.forward();  // rotate about the view axis
        if (auto d = std::dynamic_pointer_cast<Disk>(b.shape)) {
            d->normal = rotate_about(ed.x_normal, axis, a1 - a0);
        } else {
            b.spin.axis = rotate_about(ed.x_spin, axis, a1 - a0);  // sphere: spin axis (stored)
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
    render_view(ed.scene, cam, ed.time, rw, rh, ed.pixels);
    upload_texture(ed, rw, rh);

    ImVec2 img_pos = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(ed.tex)),
                 ImVec2(static_cast<float>(region_w), static_cast<float>(region_h)));
    bool hovered = ImGui::IsItemHovered();

    ScreenMap map{cam, img_pos, ImVec2(static_cast<float>(region_w),
                                       static_cast<float>(region_h))};
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Orbit overlays (selected brighter), then the selection outline on top.
    World posed = world_at_time(ed.scene, ed.time);
    for (int i = 0; i < static_cast<int>(ed.scene.bodies.size()); ++i) {
        if (!ed.scene.bodies[i].orbit.active) continue;
        draw_orbit(dl, map, ed.scene, i, ed.time, i == ed.selected ? COL_ORBIT_SEL : COL_ORBIT);
    }
    bool sel_valid = ed.selected >= 0 && ed.selected < static_cast<int>(posed.size());
    if (sel_valid) {
        Body posed_body = ed.scene.bodies[ed.selected];
        posed_body.shape = posed[ed.selected];  // posed clone carries world center
        draw_selection_outline(dl, map, posed_body, active_fov(ed));
    }

    // ---- Modal G/R/S transforms + Space to play/pause ---------------------
    ImGuiIO& io = ImGui::GetIO();
    if (ed.xmode == XMode::None && hovered && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) ed.playing = !ed.playing;
        if (sel_valid) {
            if (ImGui::IsKeyPressed(ImGuiKey_G)) begin_transform(ed, XMode::Grab);
            else if (ImGui::IsKeyPressed(ImGuiKey_R)) begin_transform(ed, XMode::Rotate);
            else if (ImGui::IsKeyPressed(ImGuiKey_S)) begin_transform(ed, XMode::Scale);
        }
    }

    bool consumed_release = false;
    if (ed.xmode != XMode::None) {
        if (!sel_valid) {
            ed.xmode = XMode::None;
        } else {
            update_transform(ed, cam, map, static_cast<float>(region_h), active_fov(ed),
                             posed[ed.selected]->center);
            dl->AddText(ImVec2(img_pos.x + 8, img_pos.y + 6), COL_OUTLINE,
                        xmode_label(ed.xmode));
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                cancel_transform(ed);
                consumed_release = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                       ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                ed.xmode = XMode::None;  // confirm: keep edited values
                consumed_release = true;
            }
        }
    }

    if (ed.xmode == XMode::None)
        handle_navigation(ed, hovered);

    // Click to select (only a plain click, not a drag and not a transform confirm).
    if (ed.xmode == XMode::None && !consumed_release && hovered &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
        ImVec2 m = io.MousePos;
        float s = (m.x - img_pos.x) / region_w;
        float t = 1.0f - (m.y - img_pos.y) / region_h;
        ed.selected = pick_body(ed, cam.get_ray(s, t));
    }

    if (sel_valid && ed.xmode == XMode::None)
        dl->AddText(ImVec2(img_pos.x + 8, img_pos.y + region_h - 22),
                    IM_COL32(210, 210, 210, 150), "G move   R rotate   S scale");

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
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
