terraform {
  required_version = ">= 1.0"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

provider "aws" {
  region = var.aws_region
}

# Variables
variable "aws_region" {
  description = "AWS region for resources"
  type        = string
  default     = "us-east-1"
}

variable "project_name" {
  description = "Name prefix for resources"
  type        = string
  default     = "esp-rover"
}

variable "environment" {
  description = "Environment name"
  type        = string
  default     = "dev"
}

# S3 bucket for audio files
resource "aws_s3_bucket" "audio_bucket" {
  bucket = "${var.project_name}-audio-${var.environment}-${random_id.bucket_suffix.hex}"
}

resource "random_id" "bucket_suffix" {
  byte_length = 4
}

resource "aws_s3_bucket_versioning" "audio_bucket_versioning" {
  bucket = aws_s3_bucket.audio_bucket.id
  versioning_configuration {
    status = "Enabled"
  }
}

resource "aws_s3_bucket_lifecycle_configuration" "audio_bucket_lifecycle" {
  bucket = aws_s3_bucket.audio_bucket.id

  rule {
    id     = "delete_old_audio"
    status = "Enabled"

    expiration {
      days = 7
    }
  }
}

# IAM role for Lambda
resource "aws_iam_role" "lambda_role" {
  name = "${var.project_name}-lambda-role-${var.environment}"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Action = "sts:AssumeRole"
        Effect = "Allow"
        Principal = {
          Service = "lambda.amazonaws.com"
        }
      }
    ]
  })
}

# IAM policy for Lambda
resource "aws_iam_role_policy" "lambda_policy" {
  name = "${var.project_name}-lambda-policy-${var.environment}"
  role = aws_iam_role.lambda_role.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect = "Allow"
        Action = [
          "logs:CreateLogGroup",
          "logs:CreateLogStream",
          "logs:PutLogEvents"
        ]
        Resource = "arn:aws:logs:${var.aws_region}:*:*"
      },
      {
        Effect = "Allow"
        Action = [
          "s3:GetObject",
          "s3:PutObject",
          "s3:DeleteObject"
        ]
        Resource = "${aws_s3_bucket.audio_bucket.arn}/*"
      },
      {
        Effect = "Allow"
        Action = [
          "transcribe:StartTranscriptionJob",
          "transcribe:GetTranscriptionJob",
          "transcribe:ListTranscriptionJobs"
        ]
        Resource = "*"
      }
    ]
  })
}

# CloudWatch Log Group for Lambda
resource "aws_cloudwatch_log_group" "lambda_logs" {
  name              = "/aws/lambda/${var.project_name}-voice-processor-${var.environment}"
  retention_in_days = 14
}

# Lambda function for voice processing
resource "aws_lambda_function" "voice_processor" {
  filename         = "voice_processor.zip"
  function_name    = "${var.project_name}-voice-processor-${var.environment}"
  role            = aws_iam_role.lambda_role.arn
  handler         = "lambda_function.lambda_handler"
  source_code_hash = data.archive_file.lambda_zip.output_base64sha256
  runtime         = "python3.11"
  timeout         = 30

  environment {
    variables = {
      AUDIO_BUCKET = aws_s3_bucket.audio_bucket.bucket
      DEBUG_MODE   = "true"
      LOG_LEVEL    = "DEBUG"
    }
  }

  depends_on = [
    aws_iam_role_policy.lambda_policy,
    aws_cloudwatch_log_group.lambda_logs
  ]
}

# Package Lambda function
data "archive_file" "lambda_zip" {
  type        = "zip"
  output_path = "voice_processor.zip"
  source_dir  = "lambda"
}

# API Gateway
resource "aws_api_gateway_rest_api" "rover_api" {
  name        = "${var.project_name}-api-${var.environment}"
  description = "API for ESP Rover voice commands"

  endpoint_configuration {
    types = ["REGIONAL"]
  }
}

# API Gateway Resource
resource "aws_api_gateway_resource" "voice_command" {
  rest_api_id = aws_api_gateway_rest_api.rover_api.id
  parent_id   = aws_api_gateway_rest_api.rover_api.root_resource_id
  path_part   = "voice-command"
}

# API Gateway Method
resource "aws_api_gateway_method" "voice_command_post" {
  rest_api_id   = aws_api_gateway_rest_api.rover_api.id
  resource_id   = aws_api_gateway_resource.voice_command.id
  http_method   = "POST"
  authorization = "NONE"
}

# API Gateway Integration
resource "aws_api_gateway_integration" "lambda_integration" {
  rest_api_id = aws_api_gateway_rest_api.rover_api.id
  resource_id = aws_api_gateway_resource.voice_command.id
  http_method = aws_api_gateway_method.voice_command_post.http_method

  integration_http_method = "POST"
  type                   = "AWS_PROXY"
  uri                    = aws_lambda_function.voice_processor.invoke_arn
}

# Lambda permission for API Gateway
resource "aws_lambda_permission" "api_gateway_lambda" {
  statement_id  = "AllowExecutionFromAPIGateway"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.voice_processor.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_api_gateway_rest_api.rover_api.execution_arn}/*/*"
}

# API Gateway Deployment
resource "aws_api_gateway_deployment" "rover_api_deployment" {
  depends_on = [
    aws_api_gateway_method.voice_command_post,
    aws_api_gateway_integration.lambda_integration
  ]

  rest_api_id = aws_api_gateway_rest_api.rover_api.id

  lifecycle {
    create_before_destroy = true
  }
}

# API Gateway Stage
resource "aws_api_gateway_stage" "rover_api_stage" {
  deployment_id = aws_api_gateway_deployment.rover_api_deployment.id
  rest_api_id   = aws_api_gateway_rest_api.rover_api.id
  stage_name    = var.environment

  xray_tracing_config {
    tracing_enabled = true
  }
}

# CloudWatch Log Group for API Gateway
resource "aws_cloudwatch_log_group" "api_gateway_logs" {
  name              = "API-Gateway-Execution-Logs_${aws_api_gateway_rest_api.rover_api.id}/${var.environment}"
  retention_in_days = 14
}

# API Gateway Method Settings for logging
resource "aws_api_gateway_method_settings" "rover_api_settings" {
  rest_api_id = aws_api_gateway_rest_api.rover_api.id
  stage_name  = aws_api_gateway_stage.rover_api_stage.stage_name
  method_path = "*/*"

  settings {
    metrics_enabled = true
    logging_level   = "INFO"
    data_trace_enabled = true
  }
}

# Outputs
output "api_gateway_url" {
  description = "API Gateway invoke URL"
  value       = "${aws_api_gateway_stage.rover_api_stage.invoke_url}/voice-command"
}

output "s3_bucket_name" {
  description = "S3 bucket name for audio files"
  value       = aws_s3_bucket.audio_bucket.bucket
}

output "lambda_function_name" {
  description = "Lambda function name"
  value       = aws_lambda_function.voice_processor.function_name
}