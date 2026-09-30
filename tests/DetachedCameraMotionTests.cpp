#include "features/camera/DetachedCameraMotion.h"
#include "features/camera/FreeCameraSprint.h"
#include <limits>
void check(bool, char const*);
void detachedCameraMotionTests() {
    using Motion = lamium::DetachedCameraMotion;
    constexpr Motion::Vector right{1,0,0}, up{0,1,0}, forward{0,0,1};
    Motion motion;
    check(!motion.begin(0), "detached movement requires an owner");
    check(motion.begin(1) && !motion.begin(1), "repeated activation retains the same session");
    check(motion.advance(1,{1,1,1},right,up,forward,10,.1), "diagonal camera input accepted");
    auto offset = *motion.snapshot();
    check(std::abs(std::hypot(offset[0],offset[1],offset[2])-1) < 1e-9,
          "three-axis movement has the configured speed");
    motion.cancel();
    check(!motion.snapshot(), "cancellation discards camera displacement");
    motion.begin(1);
    motion.advance(1,{0,0,1},right,up,forward,10,60);
    check((*motion.snapshot())[2] == 1, "stalls cannot cause a delayed movement burst");
    check(!motion.advance(2,{1,0,0},right,up,forward,10,.1) && !motion.snapshot(),
          "player replacement cancels camera movement");
    motion.begin(2);
    motion.advance(2,{.5,0,0},right,up,forward,10,.1);
    check((*motion.snapshot())[0] == .5, "analog movement retains subunit magnitude");
    check(!motion.advance(2,{0,0,0},right,up,forward,10,std::numeric_limits<double>::infinity())
          && !motion.snapshot(), "invalid timing discards the session");
    motion.begin(2);
    motion.advance(2,{0,0,1},forward,up,Motion::Vector{-1,0,0},10,.1);
    check((*motion.snapshot())[0] == -1 && (*motion.snapshot())[2] == 0,
          "movement follows the supplied camera orientation");
    motion.cancel();
    motion.begin(2);
    motion.advance(2,{1,1,1},right,up,forward,100,.1);
    auto normal = *motion.snapshot();
    motion.cancel();
    motion.begin(2);
    motion.advance(2,{1,1,1},right,up,forward,100,.1,true);
    auto boosted = *motion.snapshot();
    check(boosted[0] == normal[0] * 2 && boosted[2] == normal[2] * 2 && boosted[1] == normal[1],
          "sprint doubles horizontal motion after diagonal normalization and preserves vertical speed");
    motion.cancel();
    motion.begin(2);
    motion.advance(2,{0,0,1},right,up,forward,100,.1,true);
    check((*motion.snapshot())[2] == 20, "maximum base speed still reaches 200 blocks per second while sprinting");
    motion.advance(2,{0,0,1},right,up,forward,100,.1,false);
    check((*motion.snapshot())[2] == 30, "inactive sprint restores base speed");
    motion.cancel();
    motion.begin(2);
    motion.advance(2,{1,0,0},right,up,forward,20,.1,true);
    motion.advance(2,{0,0,-1},right,up,forward,20,.1,true);
    check(*motion.snapshot() == Motion::Vector{2,0,-2}, "strafe and backward movement never receive a sprint boost");
    lamium::camera::FreeCameraSprint sprint;
    check(sprint.update(1,true) && sprint.update(1,false), "a forward sprint continues after key release");
    check(!sprint.update(0,false) && !sprint.update(1,false), "stopping or strafing ends sprint until a new press");
    check(sprint.update(1,true) && !sprint.update(-1,false), "switching to backward movement ends sprint");
    check(!sprint.update(0,true) && !sprint.update(1,true), "a press without forward movement does not reserve sprint");
    sprint.update(1,false);
    check(sprint.update(.25,true) && sprint.update(.25,false), "analog forward movement also sustains sprint");
    check(!sprint.update(1,true,false) && !sprint.update(1,true), "menus cancel sprint without treating a held key as a fresh press");
    sprint.update(1,false);
    sprint.update(1,true);
    sprint.cancel();
    check(!sprint.update(1,true) && !sprint.update(1,false), "focus suspension cancels a latched sprint");
    sprint.update(1,true);
    sprint.reset();
    check(!sprint.active() && !sprint.update(1,false), "session reset cannot retain sprint");
    check(!sprint.update(std::numeric_limits<double>::quiet_NaN(),true), "invalid forward input cannot start sprint");
}
