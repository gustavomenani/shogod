#pragma once

// CS2 player-model bone indices, verified empirically on a live offline match
// (read-only dump of m_modelState; build with client.dll sha1
// 4E16EA32EA9485A25A67933ACD5A8B4870D855D1).
//
// Bone array: (m_pGameSceneNode + 0x140) is m_modelState; the transform array is
// *(m_modelState + 0x80), count at +0x94, stride 0x20 (Vec3 position, float
// scale, Quat). Positions are world-space. Bone 0 sits at the model origin
// (feet); it is not the pelvis.
//
// Evidence: full arrays were dumped from five standing pawns across two distinct
// models, 86-bone (T) and 94-bone (CT). Every chain edge below has the same
// length on all of them (spine 0.9/4.1/4.7/6.4/6.5/5.8, clavicle 5.4,
// clavicle->shoulder 6.8, upper arm 11.8, forearm 11.1, hip 5.4/5.3,
// thigh 17.9, shin 16.9), which pins the whole map.
//
// The same dump rules out the CS:GO layout: index 23 duplicates index 5 (it is
// never a knee), index 27 carries a non-anatomical transform far outside the
// model (not a foot), and 16/24/28..31 are weapon/finger attachment bones whose
// positions follow the held item.
//
// Only indices below sdk::k_max_bones (32) are used: the frame snapshot copies
// just the first 32 transforms.

#include <utility>

namespace shogod::sdk {
namespace bone {

inline constexpr int root = 0;        // at the feet, world position == abs origin
inline constexpr int pelvis = 1;
inline constexpr int spine_0 = 2;
inline constexpr int spine_1 = 3;
inline constexpr int spine_2 = 4;
inline constexpr int spine_3 = 5;     // chest; parent of neck and both clavicles
inline constexpr int neck = 6;
inline constexpr int head = 7;
inline constexpr int clavicle_l = 8;
inline constexpr int upperarm_l = 9;
inline constexpr int lowerarm_l = 10;
inline constexpr int hand_l = 11;
inline constexpr int clavicle_r = 12;
inline constexpr int upperarm_r = 13;
inline constexpr int lowerarm_r = 14;
inline constexpr int hand_r = 15;
inline constexpr int leg_upper_l = 17;
inline constexpr int leg_lower_l = 18;
inline constexpr int ankle_l = 19;
inline constexpr int leg_upper_r = 20;
inline constexpr int leg_lower_r = 21;
inline constexpr int ankle_r = 22;

}  // namespace bone

// Skeleton edges for the ESP overlay. Left/right are cosmetic: the two arm and
// leg chains are mirror images and both are always drawn.
inline constexpr std::pair<int, int> k_skeleton_links[] = {
    {bone::pelvis, bone::spine_0},
    {bone::spine_0, bone::spine_1},
    {bone::spine_1, bone::spine_2},
    {bone::spine_2, bone::spine_3},
    {bone::spine_3, bone::neck},
    {bone::neck, bone::head},
    {bone::spine_3, bone::clavicle_l},
    {bone::clavicle_l, bone::upperarm_l},
    {bone::upperarm_l, bone::lowerarm_l},
    {bone::lowerarm_l, bone::hand_l},
    {bone::spine_3, bone::clavicle_r},
    {bone::clavicle_r, bone::upperarm_r},
    {bone::upperarm_r, bone::lowerarm_r},
    {bone::lowerarm_r, bone::hand_r},
    {bone::pelvis, bone::leg_upper_l},
    {bone::leg_upper_l, bone::leg_lower_l},
    {bone::leg_lower_l, bone::ankle_l},
    {bone::pelvis, bone::leg_upper_r},
    {bone::leg_upper_r, bone::leg_lower_r},
    {bone::leg_lower_r, bone::ankle_r},
};

// Offscreen preview layout: 16 joints in one fixed order, shared by the live
// game provider and the menu fallback template so their connection list is the
// same.
enum preview_joint : int {
    pj_head = 0,
    pj_neck,
    pj_chest,
    pj_pelvis,
    pj_shoulder_l,
    pj_elbow_l,
    pj_hand_l,
    pj_shoulder_r,
    pj_elbow_r,
    pj_hand_r,
    pj_hip_l,
    pj_knee_l,
    pj_ankle_l,
    pj_hip_r,
    pj_knee_r,
    pj_ankle_r,
    pj_count
};

// preview joint -> CS2 bone index. The clavicles are skipped; the shoulder
// joint starts at the upper arm so the limb has three visible segments.
inline constexpr int k_preview_bone_map[pj_count] = {
    bone::head,       bone::neck,       bone::spine_3,    bone::pelvis,
    bone::upperarm_l, bone::lowerarm_l, bone::hand_l,
    bone::upperarm_r, bone::lowerarm_r, bone::hand_r,
    bone::leg_upper_l, bone::leg_lower_l, bone::ankle_l,
    bone::leg_upper_r, bone::leg_lower_r, bone::ankle_r,
};

inline constexpr std::pair<int, int> k_preview_links[] = {
    {pj_head, pj_neck},
    {pj_neck, pj_chest},
    {pj_chest, pj_pelvis},
    {pj_chest, pj_shoulder_l},
    {pj_shoulder_l, pj_elbow_l},
    {pj_elbow_l, pj_hand_l},
    {pj_chest, pj_shoulder_r},
    {pj_shoulder_r, pj_elbow_r},
    {pj_elbow_r, pj_hand_r},
    {pj_pelvis, pj_hip_l},
    {pj_hip_l, pj_knee_l},
    {pj_knee_l, pj_ankle_l},
    {pj_pelvis, pj_hip_r},
    {pj_hip_r, pj_knee_r},
    {pj_knee_r, pj_ankle_r},
};

}  // namespace shogod::sdk
