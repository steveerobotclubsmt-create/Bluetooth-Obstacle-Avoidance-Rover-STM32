#include "mbed.h"

// --- Hardware Definitions ---
DigitalOut led(LED1);
Serial     bt(PA_11, PA_12); 

AnalogIn   proxF(A0), proxL(A1), proxR(A2);   // IR Sensors
DigitalOut tF(D12), tL(D2), tR(D8);           // Trigger Pins
DigitalIn  eF(D13), eL(D4), eR(D7);           // Echo Pins

DigitalOut motL_A(PC_0), motL_B(PC_1);        // Motor Driver Pins
DigitalOut motR_A(PC_2), motR_B(PC_3); 

PwmOut     srvF(D6), srvL(D10), srvR(D11);    // Servos

// --- Control State ---
Timer      pulseTimer;
bool       isAuto = false; 
char       s_type = 'X'; 
char       activeArm = '4'; 

// ==========================================
//            ACTUATOR CONTROLS
// ==========================================
void drive(int l_a, int l_b, int r_a, int r_b, const char* msg) {
    motL_A = l_a; motL_B = l_b;
    motR_A = r_a; motR_B = r_b;
    if(msg) {
        bt.printf(" [MOTOR]: %s\r\n", msg); 
    }
}

void set_servo_pulse(PwmOut& s, int us) {
    s.period_ms(20);
    s.pulsewidth_us(us);
}

void reset_arms() {
    set_servo_pulse(srvF, 1500);
    set_servo_pulse(srvL, 1500);
    set_servo_pulse(srvR, 1500);
}

void perform_pickup() {
    bool doL = (activeArm == '1' || activeArm == '4' || activeArm == '5');
    bool doF = (activeArm == '2' || activeArm == '4' || activeArm == '5');
    bool doR = (activeArm == '3' || activeArm == '4');

    bt.printf(" >> ARM ACTION: [");
    if (doL) bt.printf(" LEFT ");
    if (doF) bt.printf(" FRONT ");
    if (doR) bt.printf(" RIGHT ");
    bt.printf("] MOVING << \r\n");
    
    if (doL) set_servo_pulse(srvL, 2000);
    if (doF) set_servo_pulse(srvF, 2000);
    if (doR) set_servo_pulse(srvR, 2000);
    wait(0.5f);
    
    reset_arms();
    wait(1.0f);
    
    if (doL) set_servo_pulse(srvL, 1000);
    if (doF) set_servo_pulse(srvF, 1000);
    if (doR) set_servo_pulse(srvR, 1000);
    wait(0.5f);
    
    reset_arms();
}

// ==========================================
//            SENSING LOGIC
// ==========================================
float get_ir_dist(AnalogIn& s) {
    float v = s.read() * 3.3f;
    return (v < 0.1f) ? 80.0f : (27.86f * pow(v, -1.15f));
}

float get_us_dist(DigitalOut& t, DigitalIn& e) {
    t = 0; wait_us(2); t = 1; wait_us(10); t = 0;
    pulseTimer.reset(); pulseTimer.start();
    while(e == 0 && pulseTimer.read_ms() < 15);
    if (e == 0) { pulseTimer.stop(); return 400.0f; }   // echo never started (sensor unplugged/dead): no reading, not 0 cm
    pulseTimer.reset();
    while(e == 1 && pulseTimer.read_ms() < 25);
    pulseTimer.stop();
    return (pulseTimer.read_ms() >= 25) ? 400.0f : (pulseTimer.read_us() / 58.0f);
}

// ==========================================
//             SYSTEM CONFIG
// ==========================================
void init_level(char lv) {
    switch(lv) {
        case '7': 
            isAuto = true;  s_type = 'I'; activeArm = '2'; 
            bt.printf("\r\n-- LVL 1 LOADED: IR & FRONT ARM --\r\n"); 
            break;
        case '8': 
            isAuto = false; s_type = 'X'; activeArm = '5'; 
            bt.printf("\r\n-- LVL 2 LOADED: MANUAL & 2-DOF ARM --\r\n"); 
            break;
        case '9': 
            isAuto = true;  s_type = 'U'; activeArm = '4'; 
            bt.printf("\r\n-- LVL 3 LOADED: US & 3-DOF ARM --\r\n"); 
            break;
    }
}

// ==========================================
//                MAIN LOOP
// ==========================================
int main() {
    bt.baud(9600);
    reset_arms();
    
    bt.printf("\r\n SYSTEM READY: SELECT LEVEL (7, 8, 9) \r\n");

    for(bool ready = false; !ready; ) {
        if(bt.readable()) {
            char c = bt.getc();
            if(c >= '7' && c <= '9') { init_level(c); ready = true; }
        }
    }

    int printTimer = 0;

    while(1) {
        if (bt.readable()) {
            char cmd = bt.getc();
            
            if(cmd == 'E' || cmd == 'e') { drive(0,0,0,0,"EMERGENCY STOP"); isAuto = false; }
            else if(cmd >= '7' && cmd <= '9') { init_level(cmd); if(!isAuto) drive(0,0,0,0, "IDLE"); }
            else if(cmd >= '1' && cmd <= '5') { activeArm = cmd; perform_pickup(); }
            else if(cmd == 'P' || cmd == 'p') { perform_pickup(); }
            
            if (!isAuto) {
                switch(cmd) {
                    case 'F': case 'f': drive(1,0,1,0, "FORWARD"); break;
                    case 'B': case 'b': drive(0,1,0,1, "REVERSE"); break;
                    case 'L': case 'l': drive(0,1,1,0, "LEFT");    break;
                    case 'R': case 'r': drive(1,0,0,1, "RIGHT");   break;
                    case 'S': case 's': case ' ': drive(0,0,0,0, "STOP"); break;
                }
            }
        }

        if (isAuto) {
            float dL, dF, dR;
            if (s_type == 'I') {
                dL = get_ir_dist(proxL); dF = get_ir_dist(proxF); dR = get_ir_dist(proxR);
            } else {
                dL = get_us_dist(tL, eL); dF = get_us_dist(tF, eF); dR = get_us_dist(tR, eR);
            }

            // Print distances every ~500ms
            if (printTimer++ >= 10) {
                bt.printf("L:%.1f F:%.1f R:%.1f\r\n", dL, dF, dR);
                printTimer = 0;
            }

            int mask = 0;
            if (dL < 10.0f) mask |= 4; 
            if (dF < 10.0f) mask |= 2; 
            if (dR < 10.0f) mask |= 1; 

            switch(mask) {
                case 0: case 1: case 4: case 5:
                    drive(1,0,1,0, NULL); 
                    break;

                case 2: // Front only
                    drive(0,0,0,0, "FRONT BLOCKED");
                    perform_pickup();
                    drive(0,1,0,1, "REVERSING..."); wait(0.5f); 
                    drive(0,1,1,0, "TURNING LEFT"); wait(0.5f);
                    break;

                case 3: // Front and Right
                    drive(0,0,0,0, "FRONT & RIGHT BLOCKED");
                    perform_pickup();
                    drive(0,1,0,1, "REVERSING..."); wait(0.5f); 
                    drive(0,1,1,0, "TURNING LEFT"); wait(0.5f);
                    break;

                case 6: // Left and Front
                    drive(0,0,0,0, "LEFT & FRONT BLOCKED");
                    perform_pickup();
                    drive(0,1,0,1, "REVERSING..."); wait(0.5f); 
                    drive(1,0,0,1, "TURNING RIGHT"); wait(0.5f);
                    break;

                case 7: // All blocked
                    drive(0,0,0,0, "DEAD END (ALL BLOCKED)");
                    perform_pickup();
                    drive(0,1,0,1, "REVERSING..."); wait(0.8f); 
                    drive(1,0,0,1, "U-TURN"); wait(1.5f);
                    break;
            }
        }
        wait_ms(50);
    }
}