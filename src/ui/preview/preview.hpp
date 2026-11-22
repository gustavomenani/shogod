#pragma once

// Offscreen 3D preview: own DX11 target, own posed SAS + rifle mesh buffers,
// shader parity with in-game chams, Fresnel glow rim, and live ESP overlay.

#include <cstdint>
#include <span>

#include "config/config.hpp"
#include "features/math/vec3.hpp"
#include "imgui.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>

namespace shogod::ui::preview {

inline constexpr int k_max_joints = 32;

enum class stance : int { combat = 0, standing = 1, a_pose = 2, count = 3 };

struct camera {
    float yaw = 64.0f;
    float pitch = 6.0f;
    float distance = 148.0f;
    math::vec3 target{6.5f, 0.0f, 26.5f};

    void reset() { *this = camera{}; }
};

class panel {
public:
    bool init(ID3D11Device* device, ID3D11DeviceContext* ctx);
    void shutdown();

    void set_stance(stance s);
    [[nodiscard]] stance current_stance() const noexcept { return stance_; }

    // Optional external skeleton override (used when A-pose / live pawn is active).
    void set_skeleton(std::span<const math::vec3> joints,
                      std::span<const std::pair<int, int>> connections);

    // Renders the model into the offscreen target and draws it plus matching
    // skeleton / ESP overlays in the current ImGui window.
    void draw(const ImVec2& size, camera& cam, const config::cham_material& material,
              bool show_skeleton, const config::visuals_cfg* esp_overlay = nullptr,
              const char* label_name = "SAS");

    [[nodiscard]] bool ready() const noexcept { return ready_; }

private:
    bool create_target(UINT w, UINT h);
    void release_target();
    void rebuild_mesh();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;
    bool ready_ = false;

    ID3D11Texture2D* color_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
    ID3D11Texture2D* depth_ = nullptr;
    ID3D11DepthStencilView* dsv_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;

    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11Buffer* vb_ = nullptr;
    ID3D11Buffer* ib_ = nullptr;
    ID3D11Buffer* cb_ = nullptr;
    ID3D11RasterizerState* rs_solid_ = nullptr;
    ID3D11RasterizerState* rs_wire_ = nullptr;
    ID3D11RasterizerState* rs_cull_front_ = nullptr;
    ID3D11DepthStencilState* ds_on_ = nullptr;
    ID3D11DepthStencilState* ds_off_ = nullptr;
    ID3D11BlendState* blend_ = nullptr;
    UINT index_count_ = 0;

    stance stance_ = stance::combat;
    math::vec3 posed_joints_[k_max_joints]{};
    math::vec3 joints_[k_max_joints]{};
    int joint_count_ = 0;
    int connections_[k_max_joints][2]{};
    int connection_count_ = 0;
    math::vec3 bounds_min_{};
    math::vec3 bounds_max_{};
    bool dirty_ = true;
};

panel& shared_panel();
camera& shared_camera();

}  // namespace shogod::ui::preview
