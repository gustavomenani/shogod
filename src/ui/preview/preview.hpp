#pragma once

// Offscreen 3D preview: own DX11 target, own mesh buffers, own camera. The
// material struct is the same cham_material the in-game chams consume, so a
// slider that changes chams changes this image in the same frame.

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

struct camera {
    float yaw = 35.0f;
    float pitch = -12.0f;
    float distance = 210.0f;
    math::vec3 target{0.0f, 0.0f, 64.0f};

    void reset() { *this = camera{}; }
};

class panel {
public:
    bool init(ID3D11Device* device, ID3D11DeviceContext* ctx);
    void shutdown();

    // Joints in world units, plus the bone connection pairs (a,b indexes).
    void set_skeleton(std::span<const math::vec3> joints,
                      std::span<const std::pair<int, int>> connections);

    // Renders the model into the offscreen target and draws it in the current
    // ImGui window. Returns the image rect for overlays.
    void draw(const ImVec2& size, camera& cam, const config::cham_material& material,
              bool show_skeleton);

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
    ID3D11Buffer* cb_ = nullptr;
    ID3D11RasterizerState* rs_solid_ = nullptr;
    ID3D11RasterizerState* rs_wire_ = nullptr;
    ID3D11DepthStencilState* ds_on_ = nullptr;
    ID3D11DepthStencilState* ds_off_ = nullptr;
    ID3D11BlendState* blend_ = nullptr;
    UINT vb_capacity_ = 0;

    math::vec3 joints_[k_max_joints]{};
    int joint_count_ = 0;
    int connections_[k_max_joints][2]{};
    int connection_count_ = 0;
    bool dirty_ = true;
};

}  // namespace shogod::ui::preview
