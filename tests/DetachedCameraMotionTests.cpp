#include "features/camera/DetachedCameraMotion.h"
#include "features/camera/FreeCameraSprint.h"
#include "features/camera/FreeCameraPosition.h"
#include <limits>
void check(bool, char const*);
void detachedCameraMotionTests() {
    {
        using Position = lamium::camera::FreeCameraPosition;
        Position position;
        constexpr Position::Vector start{100, 64, -100}, moved{105, 54, -97}, flight{2, 3, 4};
        check(!position.offset(start, {}, true), "an inactive camera has no position override");
        check(position.begin(start, false)
              && position.offset(moved, flight, false) == std::optional{flight},
              "the default reference retains player-relative camera motion");
        check(position.position(moved, flight) == std::optional{Position::Vector{107,57,-93}},
              "relative readouts follow the player's current eye and flight displacement");
        position.begin(start, true);
        for (double alpha : {0., .25, .5, .75, 1.}) {
            Position::Vector body{500, 32, 300};
            Position::Vector interpolated{500 + 20 * alpha, 32 - 5 * alpha, 300 + 10 * alpha};
            auto renderEye = Position::interpolatedEye({500,33.6,300}, body, interpolated);
            auto offset = position.offset(*renderEye, flight, true);
            Position::Vector rendered{};
            for (size_t i = 0; i < 3; ++i) rendered[i] = (*renderEye)[i] + (*offset)[i];
            check(rendered == Position::Vector{102,67,-96},
                  "world position is independent of fast body motion and the current render interpolation alpha");
        }
        check(!Position::interpolatedEye(start, moved, {0,0,std::numeric_limits<double>::infinity()}),
              "invalid interpolated native coordinates do not reach the offset writer");
        check(position.offset(moved, {}, true) == std::optional{Position::Vector{-5,10,-3}},
              "world fixation compensates body motion even before any flight input");
        check(position.position(moved, flight) == std::optional{Position::Vector{102,67,-96}},
              "world readouts stay at the activation eye plus camera flight");
        check(position.offset(moved, flight, false) == std::optional{Position::Vector{-3,13,1}},
              "switching to the player reference preserves the current camera position");
        constexpr Position::Vector next{108, 53, -95};
        check(position.position(next, flight) == std::optional{Position::Vector{105,66,-94}},
              "the rebased player reference follows subsequent body motion");
        auto before = position.position(next, flight);
        position.offset(next, flight, true);
        check(position.position(start, flight) == before,
              "switching back to world preserves the position and stops following the body");
        position.reset();
        check(!position.position(start, flight), "world exit discards the previous world's anchor");
        check(position.begin({-1000, 80, 2000}, true)
              && position.offset({-1000,80,2000}, {}, true) == std::optional{Position::Vector{}},
              "reactivation starts at the new eye with no old displacement");
        check(!position.offset({0, std::numeric_limits<double>::infinity(), 0}, {}, true)
              && !position.offset(start, {0,0,std::numeric_limits<double>::quiet_NaN()}, true),
              "invalid game coordinates and movement cannot produce a position override");
        check(!position.begin({0,std::numeric_limits<double>::quiet_NaN(),0},true)
              && !position.position(start,{}), "invalid activation discards the old anchor");
    }
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
