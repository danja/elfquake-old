EESchema Schematic File Version 2
LIBS:power
LIBS:device
LIBS:transistors
LIBS:conn
LIBS:linear
LIBS:regul
LIBS:74xx
LIBS:cmos4000
LIBS:adc-dac
LIBS:memory
LIBS:xilinx
LIBS:microcontrollers
LIBS:dsp
LIBS:microchip
LIBS:analog_switches
LIBS:motorola
LIBS:texas
LIBS:intel
LIBS:audio
LIBS:interface
LIBS:digital-audio
LIBS:philips
LIBS:display
LIBS:cypress
LIBS:siliconi
LIBS:opto
LIBS:atmel
LIBS:contrib
LIBS:valves
LIBS:VCO-cache
EELAYER 25 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 1 1
Title ""
Date ""
Rev ""
Comp ""
Comment1 ""
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
$Comp
L BC548 Q1
U 1 1 577636DE
P 7150 2100
F 0 "Q1" H 6950 2000 50  0000 L CNN
F 1 "BC548" H 6950 1900 50  0000 L CNN
F 2 "TO_SOT_Packages_THT:TO-92_Molded_Wide" H 7350 2000 50  0001 L CIN
F 3 "" H 7150 2100 50  0000 L CNN
	1    7150 2100
	1    0    0    -1  
$EndComp
$Comp
L BC548 Q2
U 1 1 577637B9
P 8150 2100
F 0 "Q2" H 8000 2300 50  0000 L CNN
F 1 "BC548" H 7850 2200 50  0000 L CNN
F 2 "TO_SOT_Packages_THT:TO-92_Molded_Wide" H 8350 2025 50  0001 L CIN
F 3 "" H 8150 2100 50  0000 L CNN
	1    8150 2100
	-1   0    0    -1  
$EndComp
$Comp
L R R2
U 1 1 5776391D
P 7250 1600
F 0 "R2" V 7330 1600 50  0000 C CNN
F 1 "1M" V 7250 1600 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 7180 1600 50  0001 C CNN
F 3 "" H 7250 1600 50  0000 C CNN
	1    7250 1600
	1    0    0    -1  
$EndComp
$Comp
L GND #PWR5
U 1 1 57763B39
P 8450 2200
F 0 "#PWR5" H 8450 1950 50  0001 C CNN
F 1 "GND" H 8450 2050 50  0000 C CNN
F 2 "" H 8450 2200 50  0000 C CNN
F 3 "" H 8450 2200 50  0000 C CNN
	1    8450 2200
	1    0    0    -1  
$EndComp
$Comp
L VCC #PWR1
U 1 1 577641C0
P 7250 1350
F 0 "#PWR1" H 7250 1200 50  0001 C CNN
F 1 "VCC" H 7250 1500 50  0000 C CNN
F 2 "" H 7250 1350 50  0000 C CNN
F 3 "" H 7250 1350 50  0000 C CNN
	1    7250 1350
	1    0    0    -1  
$EndComp
$Comp
L R R6
U 1 1 577643EA
P 7650 2800
F 0 "R6" V 7730 2800 50  0000 C CNN
F 1 "1K" V 7650 2800 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 7580 2800 50  0001 C CNN
F 3 "" H 7650 2800 50  0000 C CNN
	1    7650 2800
	1    0    0    -1  
$EndComp
$Comp
L C C1
U 1 1 57764B13
P 6600 2850
F 0 "C1" V 6550 2900 50  0000 L CNN
F 1 "1nF" V 6650 2900 50  0000 L CNN
F 2 "Capacitors_ThroughHole:C_Rect_L7_W2_P5" H 6638 2700 50  0001 C CNN
F 3 "" H 6600 2850 50  0000 C CNN
	1    6600 2850
	0    1    1    0   
$EndComp
Text Label 6600 2100 2    60   ~ 0
VIN
$Comp
L GND #PWR4
U 1 1 57765C96
P 3800 2150
F 0 "#PWR4" H 3800 1900 50  0001 C CNN
F 1 "GND" H 3800 2000 50  0000 C CNN
F 2 "" H 3800 2150 50  0000 C CNN
F 3 "" H 3800 2150 50  0000 C CNN
	1    3800 2150
	1    0    0    -1  
$EndComp
$Comp
L R R3
U 1 1 57765D3E
P 3300 1850
F 0 "R3" V 3380 1850 50  0000 C CNN
F 1 "100K" V 3300 1850 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 3230 1850 50  0001 C CNN
F 3 "" H 3300 1850 50  0000 C CNN
	1    3300 1850
	0    1    1    0   
$EndComp
$Comp
L POT RV2
U 1 1 57765EA4
P 4150 1450
F 0 "RV2" H 4150 1350 50  0000 C CNN
F 1 "10K" H 4150 1450 50  0000 C CNN
F 2 "Potentiometers:Potentiometer_Triwood_RM-065" H 4150 1450 50  0001 C CNN
F 3 "" H 4150 1450 50  0000 C CNN
	1    4150 1450
	1    0    0    -1  
$EndComp
$Comp
L R R4
U 1 1 57766E0C
P 3300 2000
F 0 "R4" V 3380 2000 50  0000 C CNN
F 1 "100K" V 3300 2000 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 3230 2000 50  0001 C CNN
F 3 "" H 3300 2000 50  0000 C CNN
	1    3300 2000
	0    1    1    0   
$EndComp
$Comp
L R R5
U 1 1 57766E52
P 3300 2150
F 0 "R5" V 3380 2150 50  0000 C CNN
F 1 "100K" V 3300 2150 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 3230 2150 50  0001 C CNN
F 3 "" H 3300 2150 50  0000 C CNN
	1    3300 2150
	0    1    1    0   
$EndComp
Text Label 3000 1850 2    60   ~ 0
KEY
Text Label 3000 2000 2    60   ~ 0
TUNE
Text Label 3000 2150 2    60   ~ 0
LFO
Text Label 5000 1950 0    60   ~ 0
VIN
Text Notes 3100 650  0    118  ~ 0
Input Adjust
$Comp
L POT RV1
U 1 1 5776968D
P 10000 1300
F 0 "RV1" H 10000 1200 50  0000 C CNN
F 1 "10K" H 10000 1300 50  0000 C CNN
F 2 "Potentiometers:Potentiometer_Triwood_RM-065" H 10000 1300 50  0001 C CNN
F 3 "" H 10000 1300 50  0000 C CNN
	1    10000 1300
	1    0    0    -1  
$EndComp
$Comp
L R R1
U 1 1 5776998B
P 9450 1300
F 0 "R1" V 9530 1300 50  0000 C CNN
F 1 "22K" V 9450 1300 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 9380 1300 50  0001 C CNN
F 3 "" H 9450 1300 50  0000 C CNN
	1    9450 1300
	0    1    1    0   
$EndComp
$Comp
L GND #PWR3
U 1 1 5776A412
P 9050 2100
F 0 "#PWR3" H 9050 1850 50  0001 C CNN
F 1 "GND" H 9050 1950 50  0000 C CNN
F 2 "" H 9050 2100 50  0000 C CNN
F 3 "" H 9050 2100 50  0000 C CNN
	1    9050 2100
	1    0    0    -1  
$EndComp
Text Notes 9300 1000 0    60   ~ 0
Current to Voltage
Text Notes 7200 600  0    118  ~ 0
Linear to exponential
$Comp
L VEE #PWR9
U 1 1 5776B898
P 9400 3550
F 0 "#PWR9" H 9400 3400 50  0001 C CNN
F 1 "VEE" H 9400 3700 50  0000 C CNN
F 2 "" H 9400 3550 50  0000 C CNN
F 3 "" H 9400 3550 50  0000 C CNN
	1    9400 3550
	-1   0    0    1   
$EndComp
$Comp
L VCC #PWR6
U 1 1 5776C08C
P 9400 2750
F 0 "#PWR6" H 9400 2600 50  0001 C CNN
F 1 "VCC" H 9400 2900 50  0000 C CNN
F 2 "" H 9400 2750 50  0000 C CNN
F 3 "" H 9400 2750 50  0000 C CNN
	1    9400 2750
	1    0    0    -1  
$EndComp
$Comp
L GND #PWR7
U 1 1 5776C238
P 9000 3300
F 0 "#PWR7" H 9000 3050 50  0001 C CNN
F 1 "GND" H 9000 3150 50  0000 C CNN
F 2 "" H 9000 3300 50  0000 C CNN
F 3 "" H 9000 3300 50  0000 C CNN
	1    9000 3300
	1    0    0    -1  
$EndComp
$Comp
L GND #PWR8
U 1 1 5776CB73
P 6200 3550
F 0 "#PWR8" H 6200 3300 50  0001 C CNN
F 1 "GND" H 6200 3400 50  0000 C CNN
F 2 "" H 6200 3550 50  0000 C CNN
F 3 "" H 6200 3550 50  0000 C CNN
	1    6200 3550
	1    0    0    -1  
$EndComp
$Comp
L R R7
U 1 1 5776D918
P 4150 5500
F 0 "R7" V 4230 5500 50  0000 C CNN
F 1 "100K" V 4150 5500 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 4080 5500 50  0001 C CNN
F 3 "" H 4150 5500 50  0000 C CNN
	1    4150 5500
	0    1    1    0   
$EndComp
$Comp
L R R9
U 1 1 5776DD2D
P 4150 5700
F 0 "R9" V 4230 5700 50  0000 C CNN
F 1 "56K" V 4150 5700 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 4080 5700 50  0001 C CNN
F 3 "" H 4150 5700 50  0000 C CNN
	1    4150 5700
	0    1    1    0   
$EndComp
Text Notes 7400 2100 0    31   ~ 0
Thermal Bond Together
$Comp
L C C2
U 1 1 57770109
P 5250 5050
F 0 "C2" V 5200 5100 50  0000 L CNN
F 1 "33nF" V 5300 5100 50  0000 L CNN
F 2 "Capacitors_ThroughHole:C_Rect_L7_W2_P5" H 5288 4900 50  0001 C CNN
F 3 "" H 5250 5050 50  0000 C CNN
	1    5250 5050
	0    1    1    0   
$EndComp
$Comp
L R R10
U 1 1 57770AB6
P 4400 6050
F 0 "R10" V 4480 6050 50  0000 C CNN
F 1 "56K" V 4400 6050 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 4330 6050 50  0001 C CNN
F 3 "" H 4400 6050 50  0000 C CNN
	1    4400 6050
	-1   0    0    1   
$EndComp
$Comp
L BC548 Q3
U 1 1 57771061
P 4650 6500
F 0 "Q3" H 4500 6700 50  0000 L CNN
F 1 "BC548" H 4350 6600 50  0000 L CNN
F 2 "TO_SOT_Packages_THT:TO-92_Molded_Wide" H 4850 6425 50  0001 L CIN
F 3 "" H 4650 6500 50  0000 L CNN
	1    4650 6500
	-1   0    0    -1  
$EndComp
$Comp
L R R11
U 1 1 57771445
P 4550 6050
F 0 "R11" V 4630 6050 50  0000 C CNN
F 1 "56K" V 4550 6050 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 4480 6050 50  0001 C CNN
F 3 "" H 4550 6050 50  0000 C CNN
	1    4550 6050
	-1   0    0    1   
$EndComp
Wire Wire Line
	8350 2100 8450 2100
Wire Wire Line
	8450 2100 8450 2200
Wire Wire Line
	7250 1900 7250 1750
Wire Wire Line
	7250 2300 7250 2450
Wire Wire Line
	7250 2450 8050 2450
Wire Wire Line
	8050 2450 8050 2300
Wire Wire Line
	7250 1350 7250 1450
Wire Wire Line
	7650 2650 7650 2450
Connection ~ 7650 2450
Wire Wire Line
	6950 3300 7650 3300
Wire Wire Line
	7250 3300 7250 2850
Wire Wire Line
	7250 2850 6750 2850
Wire Wire Line
	7650 3300 7650 2950
Connection ~ 7250 3300
Wire Wire Line
	6200 3200 6350 3200
Wire Wire Line
	6200 1850 6200 3200
Wire Wire Line
	6200 2850 6450 2850
Wire Wire Line
	6200 3550 6200 3400
Wire Wire Line
	6200 3400 6350 3400
Wire Wire Line
	6200 1850 7250 1850
Connection ~ 7250 1850
Connection ~ 6200 2850
Wire Wire Line
	6600 2100 6950 2100
Wire Wire Line
	4400 1450 4700 1450
Wire Wire Line
	4700 1250 4700 1950
Wire Wire Line
	4600 1950 5000 1950
Wire Wire Line
	3800 2150 3800 2050
Wire Wire Line
	3800 2050 4000 2050
Wire Wire Line
	4150 1300 4150 1250
Wire Wire Line
	4150 1250 4700 1250
Connection ~ 4700 1450
Wire Wire Line
	3450 1850 4000 1850
Wire Wire Line
	3600 1450 3900 1450
Wire Wire Line
	3450 2000 3600 2000
Wire Wire Line
	3600 1450 3600 2150
Connection ~ 3600 1850
Wire Wire Line
	3600 2150 3450 2150
Connection ~ 3600 2000
Wire Wire Line
	3000 1850 3150 1850
Wire Wire Line
	3000 2000 3150 2000
Wire Wire Line
	3000 2150 3150 2150
Connection ~ 4700 1950
Wire Notes Line
	2450 750  5350 750 
Wire Notes Line
	5350 750  5350 2550
Wire Notes Line
	5350 2550 2450 2550
Wire Notes Line
	2450 2550 2450 750 
Wire Wire Line
	9600 1300 9750 1300
Wire Wire Line
	10000 1150 10000 1100
Wire Wire Line
	10000 1100 9650 1100
Wire Wire Line
	9650 1100 9650 1300
Connection ~ 9650 1300
Wire Wire Line
	9850 1900 10500 1900
Wire Wire Line
	10350 1900 10350 1300
Wire Wire Line
	10350 1300 10250 1300
Wire Wire Line
	8050 1800 9250 1800
Wire Wire Line
	9050 1800 9050 1300
Wire Wire Line
	9050 1300 9300 1300
Wire Wire Line
	9050 2100 9050 2000
Wire Wire Line
	9050 2000 9250 2000
Wire Wire Line
	8050 1800 8050 1900
Connection ~ 9050 1800
Wire Notes Line
	5700 750  10900 750 
Wire Notes Line
	10900 750  10900 3900
Wire Notes Line
	10900 3900 5700 3900
Wire Notes Line
	5700 3900 5700 750 
Wire Wire Line
	9400 3450 9400 3550
Wire Wire Line
	9400 2750 9400 2850
Wire Wire Line
	9000 3300 9000 3150
Wire Wire Line
	9000 3150 9150 3150
Wire Wire Line
	9150 3050 9150 3250
Wire Wire Line
	9150 3050 9200 3050
Wire Wire Line
	9150 3250 9200 3250
Connection ~ 9150 3150
Wire Notes Line
	7400 2150 7950 2150
Wire Notes Line
	7400 2150 7450 2100
Wire Notes Line
	7400 2150 7450 2200
Wire Notes Line
	7950 2150 7900 2100
Wire Notes Line
	7950 2150 7900 2200
Wire Wire Line
	4300 5500 5000 5500
Wire Wire Line
	4550 5050 4550 5900
Wire Wire Line
	4550 5050 5100 5050
Connection ~ 4550 5500
Wire Wire Line
	5400 5050 5700 5050
Wire Wire Line
	5700 4600 5700 5600
Wire Wire Line
	5600 5600 6150 5600
Wire Wire Line
	4300 5700 5000 5700
Wire Wire Line
	4400 5900 4400 5700
Connection ~ 4400 5700
Wire Wire Line
	4550 6300 4550 6200
Wire Wire Line
	4400 6200 4400 6950
Wire Wire Line
	4400 6800 4550 6800
Wire Wire Line
	4550 6800 4550 6700
Wire Wire Line
	4000 5700 3850 5700
Wire Wire Line
	3850 5700 3850 5500
Wire Wire Line
	3850 5500 4000 5500
Wire Wire Line
	3850 5600 3600 5600
Connection ~ 3850 5600
Connection ~ 10350 1900
Text Label 10500 1900 0    60   ~ 0
VEXPO
Text Label 3600 5600 2    60   ~ 0
VEXPO
$Comp
L GND #PWR22
U 1 1 57774336
P 4400 6950
F 0 "#PWR22" H 4400 6700 50  0001 C CNN
F 1 "GND" H 4400 6800 50  0000 C CNN
F 2 "" H 4400 6950 50  0000 C CNN
F 3 "" H 4400 6950 50  0000 C CNN
	1    4400 6950
	1    0    0    -1  
$EndComp
Connection ~ 4400 6800
Connection ~ 5700 5600
$Comp
L R R13
U 1 1 57775134
P 5700 6200
F 0 "R13" V 5780 6200 50  0000 C CNN
F 1 "56K" V 5700 6200 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 5630 6200 50  0001 C CNN
F 3 "" H 5700 6200 50  0000 C CNN
	1    5700 6200
	0    1    1    0   
$EndComp
$Comp
L R R14
U 1 1 57775470
P 6350 6200
F 0 "R14" V 6430 6200 50  0000 C CNN
F 1 "100K" V 6350 6200 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 6280 6200 50  0001 C CNN
F 3 "" H 6350 6200 50  0000 C CNN
	1    6350 6200
	0    1    1    0   
$EndComp
Wire Wire Line
	6500 6200 6850 6200
Wire Wire Line
	6850 5700 6850 6500
Wire Wire Line
	6750 5700 7350 5700
Wire Wire Line
	5850 6200 6200 6200
Wire Wire Line
	6050 6200 6050 5800
Wire Wire Line
	6050 5800 6150 5800
Connection ~ 6050 6200
$Comp
L R R15
U 1 1 57775B35
P 6350 6500
F 0 "R15" V 6430 6500 50  0000 C CNN
F 1 "100K" V 6350 6500 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 6280 6500 50  0001 C CNN
F 3 "" H 6350 6500 50  0000 C CNN
	1    6350 6500
	0    1    1    0   
$EndComp
Wire Wire Line
	4850 6500 6200 6500
Wire Wire Line
	6850 6500 6500 6500
Connection ~ 6850 6200
Wire Wire Line
	5400 6200 5550 6200
Text Label 5400 6200 2    60   ~ 0
VREF
$Comp
L R R8
U 1 1 57776CA3
P 2550 6200
F 0 "R8" V 2630 6200 50  0000 C CNN
F 1 "100K" V 2550 6200 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 2480 6200 50  0001 C CNN
F 3 "" H 2550 6200 50  0000 C CNN
	1    2550 6200
	-1   0    0    1   
$EndComp
$Comp
L R R12
U 1 1 57776E4E
P 2550 6650
F 0 "R12" V 2630 6650 50  0000 C CNN
F 1 "100K" V 2550 6650 50  0000 C CNN
F 2 "Resistors_ThroughHole:Resistor_Horizontal_RM7mm" V 2480 6650 50  0001 C CNN
F 3 "" H 2550 6650 50  0000 C CNN
	1    2550 6650
	-1   0    0    1   
$EndComp
Wire Wire Line
	2550 6500 2550 6350
$Comp
L GND #PWR19
U 1 1 57777009
P 2550 6950
F 0 "#PWR19" H 2550 6700 50  0001 C CNN
F 1 "GND" H 2550 6800 50  0000 C CNN
F 2 "" H 2550 6950 50  0000 C CNN
F 3 "" H 2550 6950 50  0000 C CNN
	1    2550 6950
	1    0    0    -1  
$EndComp
Wire Wire Line
	2550 6950 2550 6800
$Comp
L VCC #PWR11
U 1 1 57777554
P 2550 5950
F 0 "#PWR11" H 2550 5800 50  0001 C CNN
F 1 "VCC" H 2550 6100 50  0000 C CNN
F 2 "" H 2550 5950 50  0000 C CNN
F 3 "" H 2550 5950 50  0000 C CNN
	1    2550 5950
	1    0    0    -1  
$EndComp
Wire Wire Line
	2550 6050 2550 5950
Wire Wire Line
	2550 6450 2700 6450
Connection ~ 2550 6450
Text Label 2700 6450 0    60   ~ 0
VREF
$Comp
L GND #PWR15
U 1 1 57778D3F
P 7550 6150
F 0 "#PWR15" H 7550 5900 50  0001 C CNN
F 1 "GND" H 7550 6000 50  0000 C CNN
F 2 "" H 7550 6150 50  0000 C CNN
F 3 "" H 7550 6150 50  0000 C CNN
	1    7550 6150
	1    0    0    -1  
$EndComp
Wire Wire Line
	7550 6150 7550 6100
$Comp
L VCC #PWR12
U 1 1 57779180
P 7550 5450
F 0 "#PWR12" H 7550 5300 50  0001 C CNN
F 1 "VCC" H 7550 5600 50  0000 C CNN
F 2 "" H 7550 5450 50  0000 C CNN
F 3 "" H 7550 5450 50  0000 C CNN
	1    7550 5450
	1    0    0    -1  
$EndComp
Wire Wire Line
	7550 5450 7550 5500
Wire Wire Line
	7350 5900 7200 5900
Wire Wire Line
	7200 5900 7200 6500
Wire Wire Line
	7200 6500 8050 6500
Wire Wire Line
	8050 6500 8050 5800
Wire Wire Line
	7950 5800 8500 5800
Connection ~ 6850 5700
Connection ~ 8050 5800
Wire Wire Line
	6700 4600 5700 4600
Connection ~ 5700 5050
Wire Wire Line
	6700 4800 6550 4800
Wire Wire Line
	6550 4800 6550 5050
Wire Wire Line
	6550 5050 7400 5050
Wire Wire Line
	7400 5050 7400 4700
Wire Wire Line
	7300 4700 8500 4700
Connection ~ 7400 4700
Text Label 8500 4700 0    60   ~ 0
TRIANGLE
Text Label 8500 5800 0    60   ~ 0
SQUARE
Wire Notes Line
	1200 7500 10900 7500
Wire Notes Line
	10900 7500 10900 4250
Wire Notes Line
	10900 4250 1200 4250
Text Notes 4250 4150 0    118  ~ 0
VCO
$Comp
L CONN_01X03 P2
U 1 1 5777D15B
P 10300 4850
F 0 "P2" H 10300 5050 50  0000 C CNN
F 1 "OUTPUT" V 10400 4850 50  0000 C CNN
F 2 "Socket_Strips:Socket_Strip_Straight_1x03" H 10300 4850 50  0001 C CNN
F 3 "" H 10300 4850 50  0000 C CNN
	1    10300 4850
	1    0    0    -1  
$EndComp
$Comp
L GND #PWR10
U 1 1 5777D544
P 9950 5050
F 0 "#PWR10" H 9950 4800 50  0001 C CNN
F 1 "GND" H 9950 4900 50  0000 C CNN
F 2 "" H 9950 5050 50  0000 C CNN
F 3 "" H 9950 5050 50  0000 C CNN
	1    9950 5050
	1    0    0    -1  
$EndComp
Wire Wire Line
	9950 5050 9950 4950
Wire Wire Line
	9950 4950 10100 4950
Wire Wire Line
	9950 4850 10100 4850
Wire Wire Line
	9950 4750 10100 4750
Text Label 9950 4750 2    60   ~ 0
TRIANGLE
Text Label 9950 4850 2    60   ~ 0
SQUARE
$Comp
L CONN_01X04 P1
U 1 1 5777E2D3
P 2700 1200
F 0 "P1" H 2700 1450 50  0000 C CNN
F 1 "VINPUTS" V 2800 1200 50  0000 C CNN
F 2 "Socket_Strips:Socket_Strip_Straight_1x04" H 2700 1200 50  0001 C CNN
F 3 "" H 2700 1200 50  0000 C CNN
	1    2700 1200
	-1   0    0    -1  
$EndComp
Text Label 3050 1050 0    60   ~ 0
KEY
Text Label 3050 1150 0    60   ~ 0
TUNE
Text Label 3050 1250 0    60   ~ 0
LFO
Wire Wire Line
	3050 1050 2900 1050
Wire Wire Line
	3050 1150 2900 1150
Wire Wire Line
	3050 1250 2900 1250
$Comp
L GND #PWR2
U 1 1 5777E9E5
P 3050 1400
F 0 "#PWR2" H 3050 1150 50  0001 C CNN
F 1 "GND" H 3050 1250 50  0000 C CNN
F 2 "" H 3050 1400 50  0000 C CNN
F 3 "" H 3050 1400 50  0000 C CNN
	1    3050 1400
	1    0    0    -1  
$EndComp
Wire Wire Line
	3050 1400 3050 1350
Wire Wire Line
	3050 1350 2900 1350
$Comp
L CONN_01X03 P3
U 1 1 57780D18
P 10300 5700
F 0 "P3" H 10300 5900 50  0000 C CNN
F 1 "POWER" V 10400 5700 50  0000 C CNN
F 2 "Socket_Strips:Socket_Strip_Straight_1x03" H 10300 5700 50  0001 C CNN
F 3 "" H 10300 5700 50  0000 C CNN
	1    10300 5700
	1    0    0    -1  
$EndComp
$Comp
L GND #PWR16
U 1 1 57780DA5
P 9950 5900
F 0 "#PWR16" H 9950 5650 50  0001 C CNN
F 1 "GND" H 9950 5750 50  0000 C CNN
F 2 "" H 9950 5900 50  0000 C CNN
F 3 "" H 9950 5900 50  0000 C CNN
	1    9950 5900
	1    0    0    -1  
$EndComp
Wire Wire Line
	9950 5900 9950 5800
Wire Wire Line
	9950 5800 10100 5800
$Comp
L VCC #PWR13
U 1 1 57781475
P 9900 5600
F 0 "#PWR13" H 9900 5450 50  0001 C CNN
F 1 "VCC" V 9900 5800 50  0000 C CNN
F 2 "" H 9900 5600 50  0000 C CNN
F 3 "" H 9900 5600 50  0000 C CNN
	1    9900 5600
	0    -1   -1   0   
$EndComp
$Comp
L VEE #PWR14
U 1 1 5778196D
P 9900 5700
F 0 "#PWR14" H 9900 5550 50  0001 C CNN
F 1 "VEE" V 9900 5900 50  0000 C CNN
F 2 "" H 9900 5700 50  0000 C CNN
F 3 "" H 9900 5700 50  0000 C CNN
	1    9900 5700
	0    -1   -1   0   
$EndComp
Wire Wire Line
	9900 5700 10100 5700
Wire Wire Line
	9900 5600 10100 5600
$Comp
L C C3
U 1 1 5778380F
P 9950 6750
F 0 "C3" H 9700 6750 50  0000 L CNN
F 1 "100nF" H 9700 6850 50  0000 L CNN
F 2 "Capacitors_ThroughHole:C_Rect_L7_W2_P5" H 9988 6600 50  0001 C CNN
F 3 "" H 9950 6750 50  0000 C CNN
	1    9950 6750
	-1   0    0    1   
$EndComp
$Comp
L GND #PWR20
U 1 1 57783F08
P 9950 7000
F 0 "#PWR20" H 9950 6750 50  0001 C CNN
F 1 "GND" H 9950 6850 50  0000 C CNN
F 2 "" H 9950 7000 50  0000 C CNN
F 3 "" H 9950 7000 50  0000 C CNN
	1    9950 7000
	1    0    0    -1  
$EndComp
Wire Wire Line
	9950 7000 9950 6900
$Comp
L VCC #PWR17
U 1 1 57784033
P 9950 6500
F 0 "#PWR17" H 9950 6350 50  0001 C CNN
F 1 "VCC" V 9950 6700 50  0000 C CNN
F 2 "" H 9950 6500 50  0000 C CNN
F 3 "" H 9950 6500 50  0000 C CNN
	1    9950 6500
	1    0    0    -1  
$EndComp
Wire Wire Line
	9950 6500 9950 6600
$Comp
L C C4
U 1 1 57784346
P 10300 6750
F 0 "C4" H 10050 6750 50  0000 L CNN
F 1 "100nF" H 10050 6850 50  0000 L CNN
F 2 "Capacitors_ThroughHole:C_Rect_L7_W2_P5" H 10338 6600 50  0001 C CNN
F 3 "" H 10300 6750 50  0000 C CNN
	1    10300 6750
	-1   0    0    1   
$EndComp
Wire Wire Line
	10300 7000 10300 6900
$Comp
L VCC #PWR18
U 1 1 57784353
P 10300 6500
F 0 "#PWR18" H 10300 6350 50  0001 C CNN
F 1 "VCC" V 10300 6700 50  0000 C CNN
F 2 "" H 10300 6500 50  0000 C CNN
F 3 "" H 10300 6500 50  0000 C CNN
	1    10300 6500
	1    0    0    -1  
$EndComp
Wire Wire Line
	10300 6500 10300 6600
$Comp
L VEE #PWR21
U 1 1 577844D1
P 10300 7000
F 0 "#PWR21" H 10300 6850 50  0001 C CNN
F 1 "VEE" V 10300 7200 50  0000 C CNN
F 2 "" H 10300 7000 50  0000 C CNN
F 3 "" H 10300 7000 50  0000 C CNN
	1    10300 7000
	-1   0    0    1   
$EndComp
$Comp
L LM358 U1
U 2 1 577A51AF
P 4300 1950
F 0 "U1" H 4250 2150 50  0000 L CNN
F 1 "LM358" H 4250 1700 50  0000 L CNN
F 2 "" H 4300 1950 50  0000 C CNN
F 3 "" H 4300 1950 50  0000 C CNN
	2    4300 1950
	1    0    0    1   
$EndComp
$Comp
L LM358 U2
U 2 1 577A537A
P 6650 3300
F 0 "U2" H 6600 3500 50  0000 L CNN
F 1 "LM358" H 6600 3050 50  0000 L CNN
F 2 "" H 6650 3300 50  0000 C CNN
F 3 "" H 6650 3300 50  0000 C CNN
	2    6650 3300
	1    0    0    1   
$EndComp
$Comp
L LM358 U2
U 1 1 577A62B0
P 9500 3150
F 0 "U2" H 9450 3350 50  0000 L CNN
F 1 "LM358" H 9450 2900 50  0000 L CNN
F 2 "" H 9500 3150 50  0000 C CNN
F 3 "" H 9500 3150 50  0000 C CNN
	1    9500 3150
	1    0    0    -1  
$EndComp
$Comp
L LM358 U1
U 1 1 577A633D
P 9550 1900
F 0 "U1" H 9500 2100 50  0000 L CNN
F 1 "LM358" H 9500 1650 50  0000 L CNN
F 2 "" H 9550 1900 50  0000 C CNN
F 3 "" H 9550 1900 50  0000 C CNN
	1    9550 1900
	1    0    0    1   
$EndComp
$Comp
L LM358 U3
U 1 1 577A7143
P 5300 5600
F 0 "U3" H 5250 5800 50  0000 L CNN
F 1 "LM358" H 5250 5350 50  0000 L CNN
F 2 "" H 5300 5600 50  0000 C CNN
F 3 "" H 5300 5600 50  0000 C CNN
	1    5300 5600
	1    0    0    1   
$EndComp
$Comp
L LM358 U4
U 2 1 577A71B6
P 7650 5800
F 0 "U4" H 7600 6000 50  0000 L CNN
F 1 "LM358" H 7600 5550 50  0000 L CNN
F 2 "" H 7650 5800 50  0000 C CNN
F 3 "" H 7650 5800 50  0000 C CNN
	2    7650 5800
	1    0    0    -1  
$EndComp
$Comp
L LM358 U3
U 2 1 577A722D
P 6450 5700
F 0 "U3" H 6400 5900 50  0000 L CNN
F 1 "LM358" H 6400 5450 50  0000 L CNN
F 2 "" H 6450 5700 50  0000 C CNN
F 3 "" H 6450 5700 50  0000 C CNN
	2    6450 5700
	1    0    0    1   
$EndComp
$Comp
L LM358 U4
U 1 1 577A72A6
P 7000 4700
F 0 "U4" H 6950 4900 50  0000 L CNN
F 1 "LM358" H 6950 4450 50  0000 L CNN
F 2 "" H 7000 4700 50  0000 C CNN
F 3 "" H 7000 4700 50  0000 C CNN
	1    7000 4700
	1    0    0    -1  
$EndComp
Wire Notes Line
	1200 4250 1200 7500
$EndSCHEMATC