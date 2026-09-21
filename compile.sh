cd ./ECU_C/src/
gcc ECUSim.c air.c fueling.c tables.c utils.c debug.c -o ../ECU -I ../include -lm
