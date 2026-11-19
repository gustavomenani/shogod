#include "ui/preview/preview.hpp"

#include "engine/log.hpp"
#include "ui/style.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace shogod::ui::preview {
namespace {

constexpr float k_pi = 3.14159265358979323846f;
constexpr UINT k_target_size = 480;

const char* k_shader_source = R"(
cbuffer Frame : register(b0) {
    row_major float4x4 view_proj;
    float4 color;
    float4 params;   // x = lighting on, y = unused
};

struct VSIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
};

struct VSOut {
    float4 pos : SV_POSITION;
    float3 nrm : NORMAL;
    float3 world : TEXCOORD0;
};

VSOut vs_main(VSIn v) {
    VSOut o;
    o.pos = mul(float4(v.pos, 1.0), view_proj);
    o.nrm = v.nrm;
    o.world = v.pos;
    return o;
}

float4 ps_main(VSOut i) : SV_TARGET {
    float3 light = normalize(float3(0.4, -0.7, 0.55));
    float ndl = saturate(dot(normalize(i.nrm), -light));
    float shade = params.x > 0.5 ? (0.32 + 0.68 * ndl) : 1.0;
    return float4(color.rgb * shade, color.a);
}
)";

struct vertex {
    float pos[3];
    float nrm[3];
};

struct cb_frame {
    float view_proj[16];
    float color[4];
    float params[4];
};

math::mat4 look_at(const math::vec3& eye, const math::vec3& target) {
    const math::vec3 z = (target - eye).normalised();
    const math::vec3 up{0.0f, 0.0f, 1.0f};
    const math::vec3 x = up.cross(z).normalised();
    const math::vec3 y = z.cross(x);

    math::mat4 out;
    out.at(0, 0) = x.x;
    out.at(0, 1) = y.x;
    out.at(0, 2) = z.x;
    out.at(0, 3) = 0.0f;
    out.at(1, 0) = x.y;
    out.at(1, 1) = y.y;
    out.at(1, 2) = z.y;
    out.at(1, 3) = 0.0f;
    out.at(2, 0) = x.z;
    out.at(2, 1) = y.z;
    out.at(2, 2) = z.z;
    out.at(2, 3) = 0.0f;
    out.at(3, 0) = -x.dot(eye);
    out.at(3, 1) = -y.dot(eye);
    out.at(3, 2) = -z.dot(eye);
    out.at(3, 3) = 1.0f;
    return out;
}

math::mat4 perspective(float fov_deg, float aspect, float zn, float zf) {
    const float y = 1.0f / std::tan(fov_deg * k_pi / 360.0f);
    const float x = y / aspect;
    math::mat4 out;
    out.at(0, 0) = x;
    out.at(1, 1) = y;
    out.at(2, 2) = zf / (zf - zn);
    out.at(2, 3) = 1.0f;
    out.at(3, 2) = -zn * zf / (zf - zn);
    return out;
}

math::mat4 multiply(const math::mat4& a, const math::mat4& b) {
    math::mat4 out;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.at(r, k) * b.at(k, c);
            out.at(r, c) = sum;
        }
    }
    return out;
}

void push_sphere(std::vector<vertex>& out, const math::vec3& centre, const math::vec3& scale,
                 const math::vec3& axis_x, const math::vec3& axis_y, const math::vec3& axis_z) {
    constexpr int k_rings = 10;
    constexpr int k_segments = 14;

    for (int r = 0; r < k_rings; ++r) {
        const float v0 = static_cast<float>(r) / k_rings * k_pi;
        const float v1 = static_cast<float>(r + 1) / k_rings * k_pi;
        for (int s = 0; s < k_segments; ++s) {
            const float u0 = static_cast<float>(s) / k_segments * 2.0f * k_pi;
            const float u1 = static_cast<float>(s + 1) / k_segments * 2.0f * k_pi;

            const float quad[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            const int order[6] = {0, 1, 2, 0, 2, 3};

            float pts[6][3]{};
            for (int k = 0; k < 6; ++k) {
                const float u = quad[order[k]][0];
                const float v = quad[order[k]][1];
                const float sx = std::sin(v) * std::cos(u);
                const float sy = std::sin(v) * std::sin(u);
                const float sz = std::cos(v);

                const math::vec3 local{sx * scale.x, sy * scale.y, sz * scale.z};
                const math::vec3 world = centre + axis_x * local.x + axis_y * local.y +
                                         axis_z * local.z;
                const math::vec3 nrm = (axis_x * sx + axis_y * sy + axis_z * sz).normalised();

                pts[k][0] = world.x;
                pts[k][1] = world.y;
                pts[k][2] = world.z;
                out.push_back(vertex{{world.x, world.y, world.z}, {nrm.x, nrm.y, nrm.z}});
            }
        }
    }
}

bool project(const math::vec3& point, const math::mat4& view_proj, const ImVec2& origin,
             const ImVec2& size, ImVec2& out) {
    const float x = view_proj.at(0, 0) * point.x + view_proj.at(1, 0) * point.y +
                    view_proj.at(2, 0) * point.z + view_proj.at(3, 0);
    const float y = view_proj.at(0, 1) * point.x + view_proj.at(1, 1) * point.y +
                    view_proj.at(2, 1) * point.z + view_proj.at(3, 1);
    const float w = view_proj.at(0, 3) * point.x + view_proj.at(1, 3) * point.y +
                    view_proj.at(2, 3) * point.z + view_proj.at(3, 3);
    if (w <= 0.001f) return false;
    out.x = origin.x + (x / w + 1.0f) * 0.5f * size.x;
    out.y = origin.y + (1.0f - y / w) * 0.5f * size.y;
    return true;
}

}  // namespace

bool panel::init(ID3D11Device* device, ID3D11DeviceContext* ctx) {
    device_ = device;
    device_->AddRef();
    ctx_ = ctx;
    ctx_->AddRef();

    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    ID3DBlob* error = nullptr;

    if (FAILED(D3DCompile(k_shader_source, std::strlen(k_shader_source), nullptr, nullptr, nullptr,
                          "vs_main", "vs_4_0", 0, 0, &vs_blob, &error))) {
        log::error("preview: vertex shader compile failed: {}",
                   error ? static_cast<const char*>(error->GetBufferPointer()) : "?");
        if (error) error->Release();
        return false;
    }
    if (FAILED(D3DCompile(k_shader_source, std::strlen(k_shader_source), nullptr, nullptr, nullptr,
                          "ps_main", "ps_4_0", 0, 0, &ps_blob, &error))) {
        log::error("preview: pixel shader compile failed: {}",
                   error ? static_cast<const char*>(error->GetBufferPointer()) : "?");
        if (error) error->Release();
        vs_blob->Release();
        return false;
    }

    device_->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr,
                                &vs_);
    device_->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &ps_);

    const D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    device_->CreateInputLayout(layout_desc, 2, vs_blob->GetBufferPointer(),
                               vs_blob->GetBufferSize(), &layout_);

    vs_blob->Release();
    ps_blob->Release();

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(cb_frame);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&cb, nullptr, &cb_);

    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    rs.DepthClipEnable = TRUE;
    device_->CreateRasterizerState(&rs, &rs_solid_);
    rs.FillMode = D3D11_FILL_WIREFRAME;
    device_->CreateRasterizerState(&rs, &rs_wire_);

    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    device_->CreateDepthStencilState(&ds, &ds_on_);
    ds.DepthEnable = FALSE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    device_->CreateDepthStencilState(&ds, &ds_off_);

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&bd, &blend_);

    ready_ = create_target(k_target_size, k_target_size);
    return ready_;
}

bool panel::create_target(UINT w, UINT h) {
    release_target();

    D3D11_TEXTURE2D_DESC cd{};
    cd.Width = w;
    cd.Height = h;
    cd.MipLevels = 1;
    cd.ArraySize = 1;
    cd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    cd.SampleDesc.Count = 1;
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&cd, nullptr, &color_))) return false;
    if (FAILED(device_->CreateRenderTargetView(color_, nullptr, &rtv_))) return false;
    if (FAILED(device_->CreateShaderResourceView(color_, nullptr, &srv_))) return false;

    D3D11_TEXTURE2D_DESC dd = cd;
    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device_->CreateTexture2D(&dd, nullptr, &depth_))) return false;
    if (FAILED(device_->CreateDepthStencilView(depth_, nullptr, &dsv_))) return false;

    width_ = w;
    height_ = h;
    return true;
}

void panel::release_target() {
    if (dsv_) { dsv_->Release(); dsv_ = nullptr; }
    if (depth_) { depth_->Release(); depth_ = nullptr; }
    if (srv_) { srv_->Release(); srv_ = nullptr; }
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
    if (color_) { color_->Release(); color_ = nullptr; }
    width_ = height_ = 0;
}

void panel::shutdown() {
    release_target();
    if (vb_) vb_->Release();
    if (cb_) cb_->Release();
    if (layout_) layout_->Release();
    if (vs_) vs_->Release();
    if (ps_) ps_->Release();
    if (rs_solid_) rs_solid_->Release();
    if (rs_wire_) rs_wire_->Release();
    if (ds_on_) ds_on_->Release();
    if (ds_off_) ds_off_->Release();
    if (blend_) blend_->Release();
    vb_ = nullptr;
    cb_ = nullptr;
    layout_ = nullptr;
    vs_ = ps_ = nullptr;
    rs_solid_ = rs_wire_ = nullptr;
    ds_on_ = ds_off_ = nullptr;
    blend_ = nullptr;

    if (ctx_) { ctx_->Release(); ctx_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
    ready_ = false;
}

void panel::set_skeleton(std::span<const math::vec3> joints,
                         std::span<const std::pair<int, int>> connections) {
    joint_count_ = std::min<int>(static_cast<int>(joints.size()), k_max_joints);
    for (int i = 0; i < joint_count_; ++i) joints_[i] = joints[i];

    connection_count_ = std::min<int>(static_cast<int>(connections.size()), k_max_joints);
    for (int i = 0; i < connection_count_; ++i) {
        connections_[i][0] = connections[i].first;
        connections_[i][1] = connections[i].second;
    }
    dirty_ = true;
}

void panel::rebuild_mesh() {
    std::vector<vertex> verts;
    verts.reserve(static_cast<std::size_t>(connection_count_ * 500 + 400));

    for (int i = 0; i < joint_count_; ++i) {
        push_sphere(verts, joints_[i], {6.0f, 6.0f, 6.0f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    }

    for (int i = 0; i < connection_count_; ++i) {
        const int a = connections_[i][0];
        const int b = connections_[i][1];
        if (a < 0 || b < 0 || a >= joint_count_ || b >= joint_count_) continue;

        const math::vec3 pa = joints_[a];
        const math::vec3 pb = joints_[b];
        const math::vec3 dir = pb - pa;
        const float len = dir.length();
        if (len < 0.001f) continue;

        const math::vec3 z = dir / len;
        const math::vec3 helper = std::fabs(z.z) < 0.99f ? math::vec3{0, 0, 1} : math::vec3{1, 0, 0};
        const math::vec3 x = z.cross(helper).normalised();
        const math::vec3 y = z.cross(x);

        push_sphere(verts, pa + dir * 0.5f, {5.0f, 5.0f, len * 0.5f}, x, y, z);
    }

    if (verts.empty()) return;

    if (vb_ == nullptr || verts.size() > vb_capacity_) {
        if (vb_) vb_->Release();
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = static_cast<UINT>(verts.size() * sizeof(vertex));
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        device_->CreateBuffer(&bd, nullptr, &vb_);
        vb_capacity_ = static_cast<UINT>(verts.size());
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(ctx_->Map(vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, verts.data(), verts.size() * sizeof(vertex));
        ctx_->Unmap(vb_, 0);
    }
    vb_capacity_ = static_cast<UINT>(verts.size());
    dirty_ = false;
}

void panel::draw(const ImVec2& size, camera& cam, const config::cham_material& material,
                 bool show_skeleton) {
    if (!ready_ || !device_) return;

    // Viewport interaction first: drag to orbit, wheel to zoom, double click resets.
    ImGui::PushID("##preview");
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##orbit", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        cam.yaw -= delta.x * 0.35f;
        cam.pitch = std::clamp(cam.pitch + delta.y * 0.3f, -85.0f, 85.0f);
    }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
        cam.distance = std::clamp(cam.distance - ImGui::GetIO().MouseWheel * 18.0f, 60.0f, 480.0f);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) cam.reset();
    ImGui::PopID();

    if (dirty_) rebuild_mesh();

    // Camera matrices.
    const math::vec3 dir{std::cos(cam.pitch * k_pi / 180.0f) * std::cos(cam.yaw * k_pi / 180.0f),
                         std::cos(cam.pitch * k_pi / 180.0f) * std::sin(cam.yaw * k_pi / 180.0f),
                         std::sin(cam.pitch * k_pi / 180.0f)};
    const math::vec3 eye = cam.target + dir * cam.distance;
    const math::mat4 view = look_at(eye, cam.target);
    const math::mat4 proj = perspective(50.0f, 1.0f, 1.0f, 2000.0f);
    const math::mat4 view_proj = multiply(view, proj);

    // Save what our pass will clobber.
    ID3D11RenderTargetView* old_rtv = nullptr;
    ID3D11DepthStencilView* old_dsv = nullptr;
    ctx_->OMGetRenderTargets(1, &old_rtv, &old_dsv);
    UINT vp_count = 1;
    D3D11_VIEWPORT old_vp{};
    ctx_->RSGetViewports(&vp_count, &old_vp);

    const float clear[4] = {0.055f, 0.06f, 0.08f, 1.0f};
    ctx_->ClearRenderTargetView(rtv_, clear);
    ctx_->ClearDepthStencilView(dsv_, D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx_->OMSetRenderTargets(1, &rtv_, dsv_);
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
    ctx_->RSSetViewports(1, &vp);

    ctx_->IASetInputLayout(layout_);
    const UINT stride = sizeof(vertex);
    const UINT offset = 0;
    ctx_->IASetVertexBuffers(0, 1, &vb_, &stride, &offset);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, &cb_);
    ctx_->PSSetConstantBuffers(0, 1, &cb_);

    const bool ignore_z = (material.flags & static_cast<int>(config::chams_flag::ignore_z)) != 0;
    const bool wireframe = (material.flags & static_cast<int>(config::chams_flag::wireframe)) != 0;
    const bool flat = (material.flags & static_cast<int>(config::chams_flag::flat)) != 0;
    const bool overlay = (material.flags & static_cast<int>(config::chams_flag::overlay)) != 0;

    ctx_->RSSetState(wireframe ? rs_wire_ : rs_solid_);
    ctx_->OMSetDepthStencilState(ignore_z ? ds_off_ : ds_on_, 0);
    if (overlay) {
        const float factor[4] = {1, 1, 1, 1};
        ctx_->OMSetBlendState(blend_, factor, 0xFFFFFFFF);
    } else {
        ctx_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    }

    const config::rgba& colour = material.enabled ? material.visible
                                                  : config::rgba{0.55f, 0.58f, 0.64f, 1.0f};
    cb_frame cb{};
    std::memcpy(cb.view_proj, &view_proj.m[0][0], sizeof cb.view_proj);
    cb.color[0] = colour.r;
    cb.color[1] = colour.g;
    cb.color[2] = colour.b;
    cb.color[3] = overlay ? colour.a : 1.0f;
    cb.params[0] = flat ? 0.0f : 1.0f;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(ctx_->Map(cb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &cb, sizeof cb);
        ctx_->Unmap(cb_, 0);
    }

    ctx_->Draw(vb_capacity_, 0);

    // Restore.
    ctx_->OMSetRenderTargets(1, &old_rtv, old_dsv);
    if (vp_count > 0) ctx_->RSSetViewports(vp_count, &old_vp);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    ctx_->OMSetDepthStencilState(nullptr, 0);
    ctx_->RSSetState(nullptr);
    if (old_rtv) old_rtv->Release();
    if (old_dsv) old_dsv->Release();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(14, 15, 20, 255), 6.0f);
    dl->AddImage(reinterpret_cast<ImTextureID>(srv_), pos,
                 ImVec2(pos.x + size.x, pos.y + size.y));
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), colors().border_soft, 6.0f);

    // Ground grid, same camera as the mesh.
    for (int i = -4; i <= 4; ++i) {
        ImVec2 a{}, b{};
        const float step = 48.0f;
        const float z = 0.0f;
        if (project({i * step, -4 * step, z}, view_proj, pos, size, a) &&
            project({i * step, 4 * step, z}, view_proj, pos, size, b)) {
            dl->AddLine(a, b, IM_COL32(40, 46, 62, 120), 1.0f);
        }
        if (project({-4 * step, i * step, z}, view_proj, pos, size, a) &&
            project({4 * step, i * step, z}, view_proj, pos, size, b)) {
            dl->AddLine(a, b, IM_COL32(40, 46, 62, 120), 1.0f);
        }
    }

    if (show_skeleton) {
        for (int i = 0; i < connection_count_; ++i) {
            const int a = connections_[i][0];
            const int b = connections_[i][1];
            if (a >= joint_count_ || b >= joint_count_) continue;
            ImVec2 pa{}, pb{};
            if (project(joints_[a], view_proj, pos, size, pa) &&
                project(joints_[b], view_proj, pos, size, pb)) {
                dl->AddLine(pa, pb, IM_COL32(255, 255, 255, 190), 1.5f);
            }
        }
        for (int i = 0; i < joint_count_; ++i) {
            ImVec2 p{};
            if (project(joints_[i], view_proj, pos, size, p)) {
                dl->AddCircleFilled(p, 2.5f, IM_COL32(120, 170, 255, 220), 8);
            }
        }
    }
}

}  // namespace shogod::ui::preview
