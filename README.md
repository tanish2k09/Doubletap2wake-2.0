# Doubletap2wake-2.0 Generic (for almost all devices)

This is just a restructured and refined code for dt2w.
Most of the parts are still from the v1.0 by Dennis Rassmann (Huge Respect!)

How to upgrade generic dt2w-2.0 in your custom kernel ?

Follow these steps if you have the good ol' v1.0 from dev Dennis Rassmann :
(For feasibility, I've added port-dt2w-functions.txt with all the necessary code)
1) Replace the whole "detect_doubletap2wake" function from v2.0 to v1.0
2) Remove "calc_feather" function from v1.0 and place "calc_within_range" function from v2.0
3) Remove "exec_count" variable declaration ("static bool exec_count") and uses from all places
Optional step 4: Change Version, author, desc, etc. Find "/* Version, author, desc, etc */" and replace next 4 lines from port file.
5) That's it! You're good to compile it now.
6) If it works nicely, make sure to give me a thanks on xda thread :D


Follow these steps if you don't even have the good ol' v1.0 from dev Dennis Rassmann :
1) Get dt2w-v1.0 by Dennis Rassmann and compile it successfully first.
2) Follow the steps in above part.
