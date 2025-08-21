

Voice-Controlled Rover Project Brief
Hardware Setup

Microcontroller: M5Stack StickC (original version)
Platform: M5Stack RoverC Pro with 4 motors and mecanum wheels 
Connectivity: WiFi enabled

Project Goal
Build a voice-controlled rover that translates speech to movement commands using AWS services.
Technical Requirements
Speech Processing

Service: AWS Transcribe (speech-to-text)
AWS Account: Already setup with necessary permissions
Infrastructure: Need Terraform code to deploy required AWS resources

User Interface

Input Method: iPhone app (ESPtouch-style interface Or webapp served on m5stickc)
App Features:

Manual controls for rover
Voice recording button for speech commands
Send commands to rover via WiFi



Programming & Development

Language Preference: Most efficient option with good library support (Arduino C++ recommended)
Experience Level: Beginner (few M5Stack projects completed)
Development Scope: Both rover firmware and AWS infrastructure needed
Rover Example code [Evaluate the code]: https://github.com/m5stack/M5-RoverC

Movement Commands

Base Commands: Forward, backward, stop, turn left, turn right
Advanced: Strafe left, strafe right, diagonal movements
Speed Control: Slow, normal, fast modes
Optimization: Utilize mecanum wheel omnidirectional capabilities

Architecture Questions to Address

Communication Protocol: How should iPhone app communicate with rover? (HTTP REST, UDP, WebSocket, MQTT via AWS IoT)
AWS Architecture: Direct iPhone→Transcribe→Rover or via API Gateway/Lambda?
iOS Development: Include iPhone app code or focus on rover + AWS only?

Deliverables Requested

Complete rover firmware (Arduino C++)
AWS infrastructure as Terraform code
Communication protocol implementation
Step-by-step setup instructions
(Optional) iOS app code if within scope

Additional Considerations

Error handling and recovery
Status feedback (LEDs, display, audio)
Smooth acceleration/deceleration for movement
Audio recording duration and trigger methods

Ask question and confirm architecture before creating any artifacts. 

additional answers:
1) i dont want a full ios app, i want to user to ESPtouch or a webapp hosted on the m5stick. there should not be a iOS app.
2) audio recording: voice activation detection. 
3) command set should inlude basic movement + straphe in different directions
4) Development Order: What should we build first - AWS infrastructure, rover firmware, etc.

make sure to review the data in the link provide from github. 