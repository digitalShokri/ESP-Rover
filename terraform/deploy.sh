#!/bin/bash

# ESP Rover AWS Infrastructure Deployment Script

set -e

echo "🚀 Deploying ESP Rover AWS Infrastructure..."

# Check if Terraform is installed
if ! command -v terraform &> /dev/null; then
    echo "❌ Terraform is not installed. Please install Terraform first."
    exit 1
fi

# Check if AWS CLI is configured
if ! aws sts get-caller-identity &> /dev/null; then
    echo "❌ AWS CLI is not configured. Please run 'aws configure' first."
    exit 1
fi

# Package Lambda function
echo "📦 Packaging Lambda function..."
cd lambda
pip install -r requirements.txt -t .
zip -r ../voice_processor.zip . -x "*.pyc" "__pycache__/*"
cd ..

# Initialize Terraform
echo "🔧 Initializing Terraform..."
terraform init

# Plan deployment
echo "📋 Planning Terraform deployment..."
terraform plan

# Ask for confirmation
read -p "Do you want to apply these changes? (y/N): " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "❌ Deployment cancelled."
    exit 1
fi

# Apply Terraform
echo "🚀 Applying Terraform configuration..."
terraform apply -auto-approve

# Output important information
echo ""
echo "✅ Deployment completed successfully!"
echo ""
echo "📝 Important outputs:"
terraform output

echo ""
echo "🔗 Next steps:"
echo "1. Note the API Gateway URL above for the rover configuration"
echo "2. Deploy the rover firmware with the API endpoint"
echo "3. Test the voice command processing"