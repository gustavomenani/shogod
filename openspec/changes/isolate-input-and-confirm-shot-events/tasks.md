## 1. Implementation
- [x] Queue native menu input with focus and overflow recovery.
- [x] Pin callbacks and retire unsafe live unload.
- [x] Validate the event bindings and log local shot and damage events.
- [x] Build the Release DLL and run regression tests.
- [x] Replace the wall thickness control with native residual damage and the existing Min Damage setting.
- [x] Check the spread sampler against the installed random stream and remove unnecessary firing waits.
- [x] Reproject recent camera clearance onto the current view and clear cached data when third person ends.

## 2. Live acceptance
- [ ] Observe input and cursor ownership through menu and focus transitions.
- [ ] Compare a missed shot with a hit against the warmup target.
- [ ] Compare estimated and observed damage through cover above and below Min Damage.
- [ ] Check third-person stability during rapid turns and beside walls.
