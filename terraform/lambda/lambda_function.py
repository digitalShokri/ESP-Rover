import json
import boto3
import base64
import uuid
import os
import logging
import time
from typing import Dict, Any, Optional

# Configure logging
log_level = os.environ.get('LOG_LEVEL', 'INFO')
logger = logging.getLogger()
logger.setLevel(getattr(logging, log_level))

# Initialize AWS clients
s3_client = boto3.client('s3')
transcribe_client = boto3.client('transcribe')

# Command mapping configuration
COMMAND_MAPPINGS = {
    # Basic movement
    'forward': {'action': 'move', 'direction': 'forward', 'speed': 'normal'},
    'move forward': {'action': 'move', 'direction': 'forward', 'speed': 'normal'},
    'go forward': {'action': 'move', 'direction': 'forward', 'speed': 'normal'},
    'backward': {'action': 'move', 'direction': 'backward', 'speed': 'normal'},
    'move backward': {'action': 'move', 'direction': 'backward', 'speed': 'normal'},
    'go backward': {'action': 'move', 'direction': 'backward', 'speed': 'normal'},
    'reverse': {'action': 'move', 'direction': 'backward', 'speed': 'normal'},
    
    # Turning
    'turn left': {'action': 'turn', 'direction': 'left', 'speed': 'normal'},
    'left': {'action': 'turn', 'direction': 'left', 'speed': 'normal'},
    'turn right': {'action': 'turn', 'direction': 'right', 'speed': 'normal'},
    'right': {'action': 'turn', 'direction': 'right', 'speed': 'normal'},
    
    # Strafing
    'strafe left': {'action': 'strafe', 'direction': 'left', 'speed': 'normal'},
    'slide left': {'action': 'strafe', 'direction': 'left', 'speed': 'normal'},
    'strafe right': {'action': 'strafe', 'direction': 'right', 'speed': 'normal'},
    'slide right': {'action': 'strafe', 'direction': 'right', 'speed': 'normal'},
    
    # Diagonal movements
    'forward left': {'action': 'diagonal', 'direction': 'forward_left', 'speed': 'normal'},
    'forward right': {'action': 'diagonal', 'direction': 'forward_right', 'speed': 'normal'},
    'backward left': {'action': 'diagonal', 'direction': 'backward_left', 'speed': 'normal'},
    'backward right': {'action': 'diagonal', 'direction': 'backward_right', 'speed': 'normal'},
    
    # Stop commands
    'stop': {'action': 'stop', 'direction': None, 'speed': None},
    'halt': {'action': 'stop', 'direction': None, 'speed': None},
    'freeze': {'action': 'stop', 'direction': None, 'speed': None},
    
    # Speed modifiers (can be combined with movement)
    'slow': {'modifier': 'speed', 'value': 'slow'},
    'fast': {'modifier': 'speed', 'value': 'fast'},
    'quickly': {'modifier': 'speed', 'value': 'fast'},
    'slowly': {'modifier': 'speed', 'value': 'slow'},
}

def lambda_handler(event: Dict[str, Any], context: Any) -> Dict[str, Any]:
    """
    Main Lambda handler for processing voice commands
    """
    try:
        logger.info(f"Received event: {json.dumps(event, default=str)}")
        
        # Parse the request
        if 'body' not in event:
            return create_error_response(400, "Missing request body")
        
        body = json.loads(event['body']) if isinstance(event['body'], str) else event['body']
        logger.debug(f"Parsed body: {json.dumps(body, default=str)}")
        
        # Extract audio data
        if 'audio' not in body:
            return create_error_response(400, "Missing audio data")
        
        audio_data = body['audio']
        rover_ip = body.get('rover_ip')
        
        # Process the audio and get command
        command = process_voice_command(audio_data, rover_ip)
        
        if command:
            logger.info(f"Successfully processed command: {command}")
            return create_success_response(command)
        else:
            logger.warning("No valid command found in audio")
            return create_error_response(400, "No valid command recognized")
            
    except Exception as e:
        logger.error(f"Error processing request: {str(e)}", exc_info=True)
        return create_error_response(500, f"Internal server error: {str(e)}")

def process_voice_command(audio_data: str, rover_ip: Optional[str] = None) -> Optional[Dict[str, Any]]:
    """
    Process base64 encoded audio data and return rover command
    """
    try:
        # Decode audio data
        audio_bytes = base64.b64decode(audio_data)
        logger.debug(f"Decoded audio data: {len(audio_bytes)} bytes")
        
        # Upload to S3
        bucket_name = os.environ['AUDIO_BUCKET']
        audio_key = f"audio/{uuid.uuid4()}.wav"
        
        s3_client.put_object(
            Bucket=bucket_name,
            Key=audio_key,
            Body=audio_bytes,
            ContentType='audio/wav'
        )
        logger.debug(f"Uploaded audio to S3: s3://{bucket_name}/{audio_key}")
        
        # Start transcription
        transcript = transcribe_audio(bucket_name, audio_key)
        
        if not transcript:
            logger.warning("No transcript generated")
            return None
        
        logger.info(f"Transcript: {transcript}")
        
        # Parse command from transcript
        command = parse_command_from_text(transcript)
        
        # Add rover IP if provided
        if command and rover_ip:
            command['rover_ip'] = rover_ip
            
        # Cleanup S3 object
        try:
            s3_client.delete_object(Bucket=bucket_name, Key=audio_key)
            logger.debug(f"Cleaned up S3 object: {audio_key}")
        except Exception as e:
            logger.warning(f"Failed to cleanup S3 object: {e}")
        
        return command
        
    except Exception as e:
        logger.error(f"Error processing voice command: {str(e)}", exc_info=True)
        return None

def transcribe_audio(bucket_name: str, audio_key: str) -> Optional[str]:
    """
    Transcribe audio using AWS Transcribe
    """
    job_name = f"rover-transcribe-{uuid.uuid4()}"
    job_uri = f"s3://{bucket_name}/{audio_key}"
    
    try:
        # Start transcription job
        transcribe_client.start_transcription_job(
            TranscriptionJobName=job_name,
            Media={'MediaFileUri': job_uri},
            MediaFormat='wav',
            LanguageCode='en-US',
            Settings={
                'ShowSpeakerLabels': False,
                'MaxSpeakerLabels': 1
            }
        )
        logger.debug(f"Started transcription job: {job_name}")
        
        # Wait for completion (with timeout)
        max_wait_time = 30  # seconds
        wait_interval = 1
        elapsed_time = 0
        
        while elapsed_time < max_wait_time:
            response = transcribe_client.get_transcription_job(
                TranscriptionJobName=job_name
            )
            
            status = response['TranscriptionJob']['TranscriptionJobStatus']
            logger.debug(f"Transcription status: {status}")
            
            if status == 'COMPLETED':
                # Get transcript
                transcript_uri = response['TranscriptionJob']['Transcript']['TranscriptFileUri']
                transcript_response = boto3.client('s3').get_object(
                    Bucket=transcript_uri.split('/')[2],
                    Key='/'.join(transcript_uri.split('/')[3:])
                )
                
                transcript_data = json.loads(transcript_response['Body'].read())
                transcript_text = transcript_data['results']['transcripts'][0]['transcript']
                
                logger.info(f"Transcription completed: {transcript_text}")
                return transcript_text.lower().strip()
                
            elif status == 'FAILED':
                logger.error("Transcription job failed")
                break
                
            time.sleep(wait_interval)
            elapsed_time += wait_interval
        
        logger.warning(f"Transcription job timed out after {max_wait_time} seconds")
        return None
        
    except Exception as e:
        logger.error(f"Error during transcription: {str(e)}", exc_info=True)
        return None
    finally:
        # Cleanup transcription job
        try:
            transcribe_client.delete_transcription_job(TranscriptionJobName=job_name)
        except:
            pass

def parse_command_from_text(text: str) -> Optional[Dict[str, Any]]:
    """
    Parse rover command from transcribed text
    """
    text = text.lower().strip()
    logger.debug(f"Parsing command from: '{text}'")
    
    # First, look for exact matches
    if text in COMMAND_MAPPINGS:
        command = COMMAND_MAPPINGS[text].copy()
        logger.debug(f"Exact match found: {command}")
        return command
    
    # Look for partial matches and combinations
    words = text.split()
    command = None
    speed_modifier = None
    
    # Check for speed modifiers
    for word in words:
        if word in COMMAND_MAPPINGS and COMMAND_MAPPINGS[word].get('modifier') == 'speed':
            speed_modifier = COMMAND_MAPPINGS[word]['value']
            break
    
    # Find the best command match
    best_match = None
    best_score = 0
    
    for phrase, cmd in COMMAND_MAPPINGS.items():
        if 'modifier' in cmd:
            continue
            
        phrase_words = phrase.split()
        score = 0
        
        # Check how many words match
        for word in phrase_words:
            if word in words:
                score += 1
        
        # Normalize score by phrase length
        normalized_score = score / len(phrase_words)
        
        if normalized_score > best_score and score > 0:
            best_score = normalized_score
            best_match = cmd.copy()
    
    if best_match:
        command = best_match
        
        # Apply speed modifier if found
        if speed_modifier:
            command['speed'] = speed_modifier
        
        logger.info(f"Command parsed: {command} (score: {best_score})")
        return command
    
    logger.warning(f"No command found for text: '{text}'")
    return None

def create_success_response(command: Dict[str, Any]) -> Dict[str, Any]:
    """
    Create successful API response
    """
    return {
        'statusCode': 200,
        'headers': {
            'Content-Type': 'application/json',
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'POST, OPTIONS',
            'Access-Control-Allow-Headers': 'Content-Type'
        },
        'body': json.dumps({
            'success': True,
            'command': command,
            'timestamp': int(time.time())
        })
    }

def create_error_response(status_code: int, message: str) -> Dict[str, Any]:
    """
    Create error API response
    """
    return {
        'statusCode': status_code,
        'headers': {
            'Content-Type': 'application/json',
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'POST, OPTIONS',
            'Access-Control-Allow-Headers': 'Content-Type'
        },
        'body': json.dumps({
            'success': False,
            'error': message,
            'timestamp': int(time.time())
        })
    }