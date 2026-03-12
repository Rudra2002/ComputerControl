unsigned long myTime;
const int pc_led_pin = D1;
const int pc_power_pin = D2;
const int pc_restart_pin = D3;
const int led_pin = LED_BUILTIN;
void setup(){
  Serial.begin(9600);
  pinMode(pc_led_pin, INPUT);
  pinMode(pc_power_pin, OUTPUT);
  pinMode(pc_restart_pin, OUTPUT);
  pinMode(led_pin, OUTPUT);

  //Blink the builtin led for checking if it is working or not
  digitalWrite(led_pin, HIGH);
  delay(500);
  digitalWrite(led_pin, LOW);
  delay(500);
  digitalWrite(led_pin, HIGH);
  delay(500);
  digitalWrite(led_pin, LOW);
  delay(500);

}
int reading(){
  return digitalRead(pc_led_pin);
}
void check_pc_status(){
  int status;
  myTime = millis();
  if(reading() == 1){
    int status = 1;
  }else{
    if(millis() - myTime >= 5){
      status = 0;
    }
    
  }
  }
void pc_power_button(){
  digitalWrite(pc_power_pin, HIGH);
  digitalWrite(led_pin, HIGH);
  delay(2000);
  digitalWrite(pc_power_pin, LOW);
}
void pc_restart_button(){
  digitalWrite(pc_restart_pin, HIGH);
  digitalWrite(led_pin, HIGH);
  delay(2000);
  digitalWrite(pc_restart_pin, LOW);
  digitalWrite(led_pin, LOW);
}



void loop(){

}