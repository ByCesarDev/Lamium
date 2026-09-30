#include "features/camera/DetachedCameraRig.h"
#include "features/camera/CameraTrace.h"
#include "app/Runtime.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/minecraft_camera/components/ActiveCameraComponent.h"
#include "mc/deps/minecraft_camera/components/CameraOffsetComponent.h"
#include "mc/deps/minecraft_camera/components/CameraRenderPlayerModelComponent.h"
#include "mc/deps/minecraft_camera/components/CameraDirectLookComponent.h"
#include "mc/deps/minecraft_camera/components/CameraOrbitComponent.h"
#include "mc/deps/vanilla_camera/components/UpdatePlayerFromCameraComponent.h"
#include <stdexcept>
#include <vector>

namespace lamium::camera::rig {
namespace {
// Vanilla turns the active camera from look input, then copies the camera's
// orientation to the player for cameras carrying UpdatePlayerFromCamera.
// Freelook withholds that component so only the camera turns. The camera's own
// look angles are saved first and written back before reattaching, so the view
// returns to the unchanged player orientation instead of turning the player.
struct DetachedCamera {
    EntityId entity;
    VanillaCamera::UpdatePlayerFromCameraComponent::LookMode mode;
    bool hasDirectLook;
    float yaw, pitch;
    // Third-person cameras keep their orientation in the orbit instead.
    bool hasOrbit = false;
    float currentAzimuth = 0, currentPolar = 0, idealAzimuth = 0, idealPolar = 0;
};
std::vector<DetachedCamera> detachedCameras; // Client thread only.
template<class Registry>
void detachOneCamera(Registry& registry, EntityId entity) {
    DetachedCamera saved{entity, registry.get<VanillaCamera::UpdatePlayerFromCameraComponent>(entity).mLookMode, false, 0, 0};
    if (auto* look = registry.try_get<MinecraftCamera::CameraDirectLookComponent>(entity)) {
        saved.hasDirectLook = true;
        saved.yaw = look->mYaw;
        saved.pitch = look->mPitch;
    }
    if (auto* orbit = registry.try_get<MinecraftCamera::CameraOrbitComponent>(entity)) {
        saved.hasOrbit = true;
        saved.currentAzimuth = orbit->mCurrentSpherical->mAzimuth;
        saved.currentPolar = orbit->mCurrentSpherical->mPolarAngle;
        saved.idealAzimuth = orbit->mIdealSpherical->mAzimuth;
        saved.idealPolar = orbit->mIdealSpherical->mPolarAngle;
    }
    registry.remove<VanillaCamera::UpdatePlayerFromCameraComponent>(entity);
    detachedCameras.push_back(saved);
}
void detachCameras(LocalPlayer& player) {
    auto& registry = player.getEntityContext().getRegistry();
    std::vector<EntityId> targets;
    for (auto entity : registry.view<MinecraftCamera::ActiveCameraComponent,
                                     VanillaCamera::UpdatePlayerFromCameraComponent>())
        targets.push_back(entity);
    if (targets.empty()) return;
    for (auto entity : targets) {
        bool saved = false;
        for (auto const& camera : detachedCameras)
            if (camera.entity == entity) { saved = true; break; }
        if (saved)
            registry.remove<VanillaCamera::UpdatePlayerFromCameraComponent>(entity);
        else
            detachOneCamera(registry, entity);
    }
#ifdef LAMIUM_CAMERA_TRACE
    try {
        Runtime::instance().self().getLogger().info("Freelook camera: detached={} directLook={} orbit={} yaw={} pitch={}",
            detachedCameras.size(), !detachedCameras.empty() && detachedCameras[0].hasDirectLook,
            !detachedCameras.empty() && detachedCameras[0].hasOrbit,
            detachedCameras.empty() ? 0.f : detachedCameras[0].yaw, detachedCameras.empty() ? 0.f : detachedCameras[0].pitch);
    } catch (...) {}
#endif
}
void restoreCameras(LocalPlayer* player) {
    if (detachedCameras.empty()) return;
    // Without the owning level the camera entities no longer exist.
    if (player) {
        auto& registry = player->getEntityContext().getRegistry();
        for (auto const& saved : detachedCameras) {
            if (!registry.valid(saved.entity)) continue;
            if (auto* look = registry.try_get<MinecraftCamera::CameraDirectLookComponent>(saved.entity);
                look && saved.hasDirectLook) {
#ifdef LAMIUM_CAMERA_TRACE
                try {
                    Runtime::instance().self().getLogger().info("Freelook camera: restore yaw={}->{} pitch={}->{}",
                        look->mYaw, saved.yaw, look->mPitch, saved.pitch);
                } catch (...) {}
#endif
                look->mYaw = saved.yaw;
                look->mPitch = saved.pitch;
                look->mYawDelta = 0.f;
            }
            if (auto* orbit = registry.try_get<MinecraftCamera::CameraOrbitComponent>(saved.entity);
                orbit && saved.hasOrbit) {
#ifdef LAMIUM_CAMERA_TRACE
                try {
                    Runtime::instance().self().getLogger().info("Freelook camera: restore orbit azimuth={}->{} polar={}->{}",
                        (float)orbit->mCurrentSpherical->mAzimuth, saved.currentAzimuth,
                        (float)orbit->mCurrentSpherical->mPolarAngle, saved.currentPolar);
                } catch (...) {}
#endif
                orbit->mCurrentSpherical->mAzimuth = saved.currentAzimuth;
                orbit->mCurrentSpherical->mPolarAngle = saved.currentPolar;
                orbit->mIdealSpherical->mAzimuth = saved.idealAzimuth;
                orbit->mIdealSpherical->mPolarAngle = saved.idealPolar;
                orbit->mAzimuthVelocity = 0.f;
                orbit->mPolarAngleVelocity = 0.f;
            }
            registry.emplace_or_replace<VanillaCamera::UpdatePlayerFromCameraComponent>(saved.entity).mLookMode = saved.mode;
        }
    }
    detachedCameras.clear();
}
// Entity-side displacement for FreeCamera. Vanilla consumes the camera
// entity's own offset while building the render view, so culling and
// overlays follow by construction instead of fighting a post-setup edit.
struct SavedCameraOffset {
    bool active = false;
    bool added = false;
    bool orbit = false; // Third-person rigs pivot instead of offsetting.
    EntityId entity;
    float x = 0, y = 0, z = 0;
    float px = 0, py = 0, pz = 0;
};
SavedCameraOffset savedOffset; // Mirrors the detachedCameras lifetime rules.
void takeFreeCameraOffset(LocalPlayer& player, EntityId entity) {
    auto& registry = player.getEntityContext().getRegistry();
    if (!registry.valid(entity)) throw std::runtime_error("FreeCamera camera entity is gone");
    savedOffset = {};
    auto* offset = registry.try_get<MinecraftCamera::CameraOffsetComponent>(entity);
    if (!offset) {
        registry.emplace<MinecraftCamera::CameraOffsetComponent>(entity);
        offset = registry.try_get<MinecraftCamera::CameraOffsetComponent>(entity);
        if (!offset) throw std::runtime_error("FreeCamera cannot attach a camera offset");
        savedOffset.added = true;
    }
    savedOffset.active = true;
    savedOffset.entity = entity;
    savedOffset.x = (*offset->mEntityOffset).x;
    savedOffset.y = (*offset->mEntityOffset).y;
    savedOffset.z = (*offset->mEntityOffset).z;
    // Orbit cameras ignore the entity offset; their rig pivots instead.
    if (registry.try_get<MinecraftCamera::CameraOrbitComponent>(entity)) {
        savedOffset.orbit = true;
        savedOffset.px = (*offset->mPivot).x;
        savedOffset.py = (*offset->mPivot).y;
        savedOffset.pz = (*offset->mPivot).z;
    }
}
void restoreFreeCameraOffset(LocalPlayer* player) {
    if (!savedOffset.active) return;
    savedOffset.active = false;
    try {
        if (!player) return; // Level gone; its entities are dead anyway.
        auto& registry = player->getEntityContext().getRegistry();
        if (!registry.valid(savedOffset.entity)) return;
        auto* offset = registry.try_get<MinecraftCamera::CameraOffsetComponent>(savedOffset.entity);
        if (!offset) return;
        if (savedOffset.added) registry.remove<MinecraftCamera::CameraOffsetComponent>(savedOffset.entity);
        else {
            (*offset->mEntityOffset).x = savedOffset.x;
            (*offset->mEntityOffset).y = savedOffset.y;
            (*offset->mEntityOffset).z = savedOffset.z;
            if (savedOffset.orbit) {
                (*offset->mPivot).x = savedOffset.px;
                (*offset->mPivot).y = savedOffset.py;
                (*offset->mPivot).z = savedOffset.pz;
            }
        }
    } catch (...) {
        Runtime::instance().self().getLogger().error("FreeCamera could not restore the camera offset");
    }
}
struct SavedBodyRender {
    bool added = false;
    EntityId entity;
};
SavedBodyRender savedBody; // First-person rigs hide the body; show it.
void takeFreeCameraBody(LocalPlayer& player, EntityId entity) {
    savedBody = {};
    auto& registry = player.getEntityContext().getRegistry();
    if (!registry.valid(entity)) return;
    // Direct-look rigs render hands, not the body. Orbit rigs already show it.
    if (!registry.try_get<MinecraftCamera::CameraDirectLookComponent>(entity)) return;
    // Empty tag: entt cannot try_get it, so test membership instead.
    if (registry.all_of<MinecraftCamera::CameraRenderPlayerModelComponent>(entity)) return;
    registry.emplace<MinecraftCamera::CameraRenderPlayerModelComponent>(entity);
    savedBody.added = true;
    savedBody.entity = entity;
}
void restoreFreeCameraBody(LocalPlayer* player) {
    if (!savedBody.added) return;
    savedBody.added = false;
    try {
        if (!player) return;
        auto& registry = player->getEntityContext().getRegistry();
        if (!registry.valid(savedBody.entity)) return;
        if (registry.all_of<MinecraftCamera::CameraRenderPlayerModelComponent>(savedBody.entity))
            registry.remove<MinecraftCamera::CameraRenderPlayerModelComponent>(savedBody.entity);
    } catch (...) {
        Runtime::instance().self().getLogger().error("FreeCamera could not restore body rendering");
    }
}
}
void detach(LocalPlayer& player) { detachCameras(player); }
void restore(LocalPlayer* player) {
    try {
        restoreCameras(player);
    } catch (...) {
        detachedCameras.clear();
        Runtime::instance().self().getLogger().error("Freelook could not restore the camera");
    }
}
bool detached() { return !detachedCameras.empty(); }
bool firstPerson(LocalPlayer& player) {
    auto& registry = player.getEntityContext().getRegistry();
    for (auto entity : registry.view<MinecraftCamera::ActiveCameraComponent>()) {
        if (!registry.valid(entity)) continue;
        if (registry.try_get<MinecraftCamera::CameraDirectLookComponent>(entity)) return true;
    }
    return false;
}
void takeFreeCamera(LocalPlayer& player) {
    if (detachedCameras.empty()) throw std::runtime_error("FreeCamera has no detached camera entity");
    takeFreeCameraOffset(player, detachedCameras.back().entity);
    takeFreeCameraBody(player, detachedCameras.back().entity);
}
void restoreFreeCamera(LocalPlayer* player) {
    restoreFreeCameraOffset(player);
    restoreFreeCameraBody(player);
}
bool writeOffset(LocalPlayer& player, DetachedCameraMotion::Vector const& displacement) {
    if (detachedCameras.empty()) return false;
    auto& registry = player.getEntityContext().getRegistry();
    auto entity = detachedCameras.back().entity;
    // Perspective switches are suppressed while detached (see the F5
    // hook), so the activation rig stays current; anything else keeps
    // vanilla values instead of steering a stale rig.
    if (!registry.valid(entity)) return false;
    auto* offset = registry.try_get<MinecraftCamera::CameraOffsetComponent>(entity);
    if (!offset) return false;
    // F5 may morph the rig (direct-look converts to orbit or back), so
    // read the live shape instead of the activation-time flag.
    bool orbit = registry.try_get<MinecraftCamera::CameraOrbitComponent>(entity) != nullptr;
    bool same = savedOffset.active && savedOffset.entity == entity;
    trace::writer(same, orbit, displacement);
    if (same && orbit) {
        // Third person: swing the pivot, keep the vanilla entity offset.
        (*offset->mPivot).x = savedOffset.px + static_cast<float>(displacement[0]);
        (*offset->mPivot).y = savedOffset.py + static_cast<float>(displacement[1]);
        (*offset->mPivot).z = savedOffset.pz + static_cast<float>(displacement[2]);
    } else if (same) {
        (*offset->mEntityOffset).x = static_cast<float>(displacement[0]);
        (*offset->mEntityOffset).y = static_cast<float>(displacement[1]);
        (*offset->mEntityOffset).z = static_cast<float>(displacement[2]);
    }
    // A different entity (perspective swap) keeps vanilla values until a
    // re-take lands; writing blind would steer the wrong rig.
    return same;
}
}
