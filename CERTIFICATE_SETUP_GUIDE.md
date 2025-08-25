# ESP Rover - SSL Certificate Setup Guide

## Quick Start (5 minutes)

### Step 1: Generate Development Certificates
```bash
cd firmware/esp_rover_secure
chmod +x generate_certificates.sh
./generate_certificates.sh
```

### Step 2: Update Your Network Configuration
Edit `ssl_config.cnf` and update the IP addresses to match your network:
```ini
[alt_names]
DNS.1 = esp-rover-secure.local
IP.1 = 192.168.1.100    # Change this to your rover's expected IP
IP.2 = 192.168.4.1      # AP mode IP (don't change)
IP.3 = 192.168.1.200    # Add backup IP if needed
```

### Step 3: Re-run Certificate Generation
```bash
./generate_certificates.sh
```

### Step 4: Include Certificates in Firmware
The script automatically creates `certificates/server_cert.h` which will be included in your firmware build.

### Step 5: Flash and Test
1. Compile and upload firmware to M5StickC
2. Connect to rover's WiFi or configure your WiFi
3. Navigate to `https://<rover-ip>:8443`
4. Accept browser security warning (this is expected for self-signed certificates)

## Detailed Setup Options

### Option A: Self-Signed Certificates (Recommended for Development)

**Best for**: Testing, development, internal networks

1. **Generate certificates**:
   ```bash
   cd firmware/esp_rover_secure
   ./generate_certificates.sh
   ```

2. **Customize for your network** (edit `ssl_config.cnf`):
   ```ini
   # Add your router's typical IP range
   IP.1 = 192.168.1.100
   IP.2 = 192.168.0.100  
   IP.3 = 10.0.0.100
   ```

3. **Regenerate if needed**:
   ```bash
   rm -rf certificates/
   ./generate_certificates.sh
   ```

### Option B: Let's Encrypt (For Internet-Accessible Rovers)

**Requirements**: Domain name pointing to your rover, internet access

1. **Set up dynamic DNS** (using services like DuckDNS):
   ```bash
   # Example with DuckDNS
   curl "https://www.duckdns.org/update?domains=myrover&token=your-token&ip="
   ```

2. **Install certbot**:
   ```bash
   sudo apt-get update
   sudo apt-get install certbot
   ```

3. **Generate certificate**:
   ```bash
   sudo certbot certonly --standalone -d myrover.duckdns.org
   ```

4. **Copy to ESP32 project**:
   ```bash
   sudo cp /etc/letsencrypt/live/myrover.duckdns.org/fullchain.pem certificates/server.crt
   sudo cp /etc/letsencrypt/live/myrover.duckdns.org/privkey.pem certificates/server.key
   chmod 644 certificates/server.*
   ```

5. **Convert to header format**:
   ```bash
   # The generate_certificates.sh script will convert existing .crt and .key files
   ./generate_certificates.sh
   ```

### Option C: Upload via Web Interface (Future Feature)

The firmware includes support for uploading certificates via the web interface, though this requires the rover to be running first with self-signed certificates.

## Browser Configuration

### Accepting Self-Signed Certificates

**Chrome**:
1. Navigate to `https://<rover-ip>:8443`
2. Click "Advanced"
3. Click "Proceed to [IP] (unsafe)"
4. Bookmark for easy access

**Firefox**:
1. Navigate to `https://<rover-ip>:8443`
2. Click "Advanced"
3. Click "Accept the Risk and Continue"

**Safari**:
1. Navigate to `https://<rover-ip>:8443`
2. Click "Show Details"
3. Click "Visit this website"
4. Click "Visit Website" in popup

### Adding Certificate to System Trust Store (Advanced)

**macOS**:
```bash
# Add to system keychain
sudo security add-trusted-cert -d -r trustRoot -k /Library/Keychains/System.keychain certificates/server.crt
```

**Linux**:
```bash
# Copy to system certificates
sudo cp certificates/server.crt /usr/local/share/ca-certificates/esp-rover.crt
sudo update-ca-certificates
```

**Windows**:
1. Double-click `certificates/server.crt`
2. Click "Install Certificate"
3. Choose "Local Machine" 
4. Choose "Place all certificates in the following store"
5. Browse → "Trusted Root Certification Authorities"

## Network-Specific Setup

### Home Network (192.168.1.x)
```ini
# In ssl_config.cnf
IP.1 = 192.168.1.100
IP.2 = 192.168.1.200
```

### Office Network (10.0.x.x)
```ini
# In ssl_config.cnf  
IP.1 = 10.0.0.100
IP.2 = 10.0.1.100
```

### Multiple Networks
```ini
# Support multiple network ranges
IP.1 = 192.168.1.100
IP.2 = 192.168.0.100
IP.3 = 10.0.0.100
IP.4 = 172.16.0.100
```

## Production Deployment

### Enterprise Certificate Authority

If your organization has an internal CA:

1. **Generate CSR**:
   ```bash
   openssl req -new -key certificates/server.key -out certificates/server.csr -config ssl_config.cnf
   ```

2. **Submit CSR to your CA** for signing

3. **Receive signed certificate** and place in `certificates/server.crt`

4. **Update firmware**:
   ```bash
   ./generate_certificates.sh
   ```

### Commercial SSL Certificate

For maximum browser compatibility:

1. **Purchase SSL certificate** from trusted CA (DigiCert, GlobalSign, etc.)

2. **Generate CSR** with the script:
   ```bash
   ./generate_certificates.sh
   # Use the generated server.csr file
   ```

3. **Submit CSR to CA** for validation

4. **Install received certificate**:
   ```bash
   # Replace server.crt with the signed certificate
   cp your-signed-certificate.crt certificates/server.crt
   ./generate_certificates.sh
   ```

## Troubleshooting

### Common Issues

#### "Certificate Not Found" Error
```
ERROR: Failed to load SSL certificates
```

**Solution**: Ensure certificates are properly generated:
```bash
ls -la certificates/
# Should show server.crt, server.key, server_cert.h
```

#### Browser Shows "Not Secure"
This is expected for self-signed certificates. Click "Advanced" and proceed.

#### "SSL Handshake Failed"
**Possible causes**:
- Certificate doesn't match IP/hostname
- System time is incorrect
- Certificate is corrupted

**Solutions**:
1. **Regenerate certificates** with correct IP addresses
2. **Check ESP32 time synchronization** (should show NTP sync in serial monitor)
3. **Verify certificate format** (should start with `-----BEGIN CERTIFICATE-----`)

#### Out of Memory Errors
**Symptom**: ESP32 crashes or won't start after adding certificates

**Solutions**:
1. **Reduce certificate size**: Use shorter validity periods
2. **Optimize other memory usage**: Reduce JSON buffer sizes in config.h
3. **Use smaller key sizes**: 2048-bit instead of 4096-bit

### Debug Commands

**Check certificate details**:
```bash
openssl x509 -in certificates/server.crt -text -noout
```

**Verify certificate/key match**:
```bash
openssl x509 -noout -modulus -in certificates/server.crt | openssl md5
openssl rsa -noout -modulus -in certificates/server.key | openssl md5
# MD5 hashes should match
```

**Test SSL connection**:
```bash
openssl s_client -connect <rover-ip>:8443
```

### Serial Monitor Debugging

Enable SSL debug output by checking the serial monitor at 115200 baud:

```
SSL Certificate Manager initialized successfully
Certificate details:
- Server cert length: 1234 bytes
- Private key length: 1678 bytes
NTP time synchronized: Mon Aug 25 15:30:45 2025
Primary HTTPS Server started on port 443
Fallback HTTPS Server started on port 8443
```

## Security Considerations

### Development vs Production

**Development (Self-signed)**:
✅ Quick setup
✅ No external dependencies
❌ Browser warnings
❌ No identity verification

**Production (CA-signed)**:
✅ Trusted by browsers
✅ Full identity verification
✅ Professional appearance
❌ Requires domain name or static IP
❌ Costs money (unless using Let's Encrypt)

### Best Practices

1. **Never commit private keys** to version control
2. **Use strong passwords** if encrypting private keys
3. **Set appropriate certificate expiration** (1 year for development, longer for production)
4. **Monitor certificate expiration** and renew before expiry
5. **Use proper file permissions** (600 for .key files)

## Maintenance

### Certificate Renewal

**Self-signed** (annual):
```bash
cd firmware/esp_rover_secure
./generate_certificates.sh
# Flash updated firmware
```

**Let's Encrypt** (automated):
```bash
# Set up cron job for renewal
echo "0 12 * * * /usr/bin/certbot renew --quiet && ./update_rover_certs.sh" | crontab -
```

**Commercial** (manual):
- Set calendar reminders 30 days before expiration
- Renew through CA provider
- Update certificates in firmware

---

## Summary

For most development and testing scenarios:
1. Run `./generate_certificates.sh`
2. Flash firmware
3. Accept browser security warnings
4. Enjoy secure HTTPS communication!

The rover will work with self-signed certificates and provide full encryption, even though browsers will show warnings. For production deployment, use certificates from a trusted CA for the best user experience.