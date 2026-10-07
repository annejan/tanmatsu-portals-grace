# DLC

Extra chambers, for the SD card. **Spoilers:** each file here ends with
its solution.

| Chamber | |
|---|---|
| `overtime.txt` | **Overtime**: everything you have learned, in the right order. A turret, a reflection cube through a floor-to-ceiling portal, a sphere's cup behind a glass slot, a laser catcher, and a funnel up to a high exit. |
| `momentum.txt` | **Momentum**: nothing in there gives you speed. Paint your own runway with orange gel through a portal, throw a cube at that speed, and fall without end to come out flying. |

`make dlc-upload` checks every one is solved (twelve times, at the
badge's random 20 to 35 frames a second), then puts them on the badge's
card in `/sd/portals/chambers/`, without their solutions; the solutions
go to `/sd/portals/recordings/dlc-solutions.txt`, to watch with Esc →
Watch a recording. They appear after the built-in chambers in Chamber
select.

`make dlc-movies` films each, as the game's own chambers are filmed:
`build/dlc/<id>.mp4`.

A chamber file is described in `chambers/README.md`; the editor on the
badge makes them too.
