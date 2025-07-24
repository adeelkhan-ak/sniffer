# VoIPmonitor Deployment Guide

## System Requirements

### Hardware Requirements

#### Minimum Requirements
- **CPU**: 2-core x86_64 processor (Intel/AMD)
- **RAM**: 4GB minimum (8GB recommended)
- **Storage**: 100GB available disk space
- **Network**: Gigabit Ethernet interface

#### Recommended Production Requirements
- **CPU**: 8+ cores with high clock speed (3.0GHz+)
- **RAM**: 16GB+ (32GB+ for high-volume environments)
- **Storage**: SSD storage with 500GB+ available space
- **Network**: 10Gbps interface for high-traffic monitoring
- **RAID**: RAID 1 or RAID 10 for database storage

#### High-Volume Enterprise Requirements
- **CPU**: 16+ cores, Intel Xeon or AMD EPYC
- **RAM**: 64GB+ with ECC memory
- **Storage**: NVMe SSD with 2TB+ capacity
- **Network**: Multiple 10Gbps interfaces or 25Gbps+
- **Database**: Dedicated database server with similar specs

### Software Requirements

#### Operating System
- **Primary**: Linux (Ubuntu 20.04+, CentOS 8+, RHEL 8+)
- **Alternative**: FreeBSD 12+
- **Kernel**: 3.10+ (4.x+ recommended for optimal performance)

#### Dependencies
- **Database**: MySQL 8.0+ or MariaDB 10.5+
- **Libraries**: 
  - libpcap-dev (1.8+)
  - libmysqlclient-dev
  - libssl-dev (OpenSSL 1.1+)
  - libvorbis-dev
  - libsnappy-dev
  - librrd-dev
  - libjson-c-dev
  - libcurl4-openssl-dev

## Installation Methods

### 1. Package Installation (Recommended)

#### Ubuntu/Debian
```bash
# Add VoIPmonitor repository
wget -O - http://www.voipmonitor.org/debian/voipmonitor-key.pub | apt-key add -
echo "deb http://www.voipmonitor.org/debian/ stable main" > /etc/apt/sources.list.d/voipmonitor.list

# Update package list
apt-get update

# Install VoIPmonitor
apt-get install voipmonitor-sniffer

# Install MySQL/MariaDB
apt-get install mariadb-server mariadb-client
```

#### CentOS/RHEL
```bash
# Add VoIPmonitor repository
rpm --import http://www.voipmonitor.org/centos/voipmonitor-key.pub
cat > /etc/yum.repos.d/voipmonitor.repo << EOF
[voipmonitor]
name=VoIPmonitor repository
baseurl=http://www.voipmonitor.org/centos/\$releasever/\$basearch/
enabled=1
gpgcheck=1
gpgkey=http://www.voipmonitor.org/centos/voipmonitor-key.pub
EOF

# Install VoIPmonitor
yum install voipmonitor-sniffer

# Install MySQL/MariaDB
yum install mariadb-server mariadb
```

### 2. Source Compilation

#### Prerequisites Installation
```bash
# Ubuntu/Debian
apt-get install build-essential autoconf automake libtool pkg-config
apt-get install libpcap-dev libmysqlclient-dev libssl-dev libvorbis-dev
apt-get install libsnappy-dev librrd-dev libjson-c-dev libcurl4-openssl-dev

# CentOS/RHEL
yum groupinstall "Development Tools"
yum install libpcap-devel mysql-devel openssl-devel libvorbis-devel
yum install snappy-devel rrdtool-devel json-c-devel libcurl-devel
```

#### Compilation Process
```bash
# Download source code
git clone https://github.com/voipmonitor/sniffer.git
cd sniffer

# Configure build
./configure --enable-shared

# Compile
make -j$(nproc)

# Install
make install

# Create systemd service
cp config/systemd/voipmonitor.service /etc/systemd/system/
systemctl daemon-reload
```

### 3. Docker Deployment

#### Docker Compose Setup
```yaml
version: '3.8'
services:
  voipmonitor:
    image: voipmonitor/sniffer:latest
    container_name: voipmonitor
    network_mode: host
    privileged: true
    volumes:
      - ./config:/etc/voipmonitor
      - ./data:/var/spool/voipmonitor
      - ./logs:/var/log/voipmonitor
    environment:
      - MYSQL_HOST=mysql
      - MYSQL_DATABASE=voipmonitor
      - MYSQL_USER=voipmonitor
      - MYSQL_PASSWORD=secure_password
    depends_on:
      - mysql
    restart: unless-stopped

  mysql:
    image: mariadb:10.8
    container_name: voipmonitor-mysql
    environment:
      - MYSQL_ROOT_PASSWORD=root_password
      - MYSQL_DATABASE=voipmonitor
      - MYSQL_USER=voipmonitor
      - MYSQL_PASSWORD=secure_password
    volumes:
      - mysql_data:/var/lib/mysql
      - ./mysql-config:/etc/mysql/conf.d
    ports:
      - "3306:3306"
    restart: unless-stopped

volumes:
  mysql_data:
```

## Database Setup

### 1. MySQL/MariaDB Configuration

#### Create Database and User
```sql
-- Connect as root
mysql -u root -p

-- Create database
CREATE DATABASE voipmonitor CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- Create user
CREATE USER 'voipmonitor'@'localhost' IDENTIFIED BY 'secure_password';
GRANT ALL PRIVILEGES ON voipmonitor.* TO 'voipmonitor'@'localhost';
FLUSH PRIVILEGES;
```

#### Optimize MySQL Configuration
```ini
# /etc/mysql/mariadb.conf.d/50-voipmonitor.cnf
[mysqld]
# Memory settings
innodb_buffer_pool_size = 4G
innodb_log_file_size = 512M
innodb_log_buffer_size = 64M
key_buffer_size = 256M
sort_buffer_size = 2M
read_buffer_size = 2M
read_rnd_buffer_size = 8M
myisam_sort_buffer_size = 64M
table_open_cache = 4000
thread_cache_size = 50

# Performance settings
innodb_flush_log_at_trx_commit = 2
innodb_file_per_table = 1
innodb_thread_concurrency = 0
innodb_read_io_threads = 8
innodb_write_io_threads = 8
innodb_io_capacity = 1000

# Query cache (if using MySQL 5.7)
query_cache_type = 1
query_cache_size = 256M
query_cache_limit = 2M

# Binary logging
log-bin = mysql-bin
binlog_format = mixed
expire_logs_days = 7

# Partitioning support
partition = ON

# Character set
character-set-server = utf8mb4
collation-server = utf8mb4_unicode_ci

# Connection settings
max_connections = 500
max_connect_errors = 10000
wait_timeout = 28800
interactive_timeout = 28800

# Slow query log
slow_query_log = 1
slow_query_log_file = /var/log/mysql/slow.log
long_query_time = 2
log_queries_not_using_indexes = 1
```

### 2. Database Schema Creation

#### Automatic Schema Creation
```bash
# VoIPmonitor will create tables automatically on first run
voipmonitor --config-file=/etc/voipmonitor.conf --create-database-tables
```

#### Manual Schema Import
```bash
# Download schema files
wget http://www.voipmonitor.org/download/sql/voipmonitor_mysql_create.sql

# Import schema
mysql -u voipmonitor -p voipmonitor < voipmonitor_mysql_create.sql
```

## Configuration

### 1. Main Configuration File

#### Basic Configuration (`/etc/voipmonitor.conf`)
```ini
# Database configuration
mysql_server = localhost
mysql_database = voipmonitor
mysql_username = voipmonitor
mysql_password = secure_password
mysql_port = 3306

# Network interface
interface = eth0
# Or capture from multiple interfaces
# interface = eth0,eth1,eth2

# Packet capture settings
ringbuffer = 50
packetbuffer_enable = yes
packetbuffer_compress = lz4
packetbuffer_file_totalmaxsize = 2000

# Call processing
savesip = yes
savertp = yes
saveregister = yes
saveaudio = yes
audioformat = wav

# Quality analysis
mos_lqo = yes
mos_lqo_bin = /usr/local/bin/pesq
mos_lqo_ref = /usr/share/voipmonitor/audio/mos_lqo_ref.wav

# Fraud detection
fraud_detection = yes
fraud_time_period_anonymous = 300
fraud_time_period_partner = 3600

# Management interface
manager_port = 5029
manager_password = secure_manager_password

# Logging
sqlcallend = yes
printinsertid = no
cleandatabase_cdr = 365
cleandatabase_register_state = 30
cleandatabase_register_failed = 7

# Performance tuning
ringbuffer = 500
pcap_queue_block_max_size = 500
pcap_queue_store_queue_max_memory_size = 1000
pcap_queue_store_queue_max_disk_size = 10000

# SSL/TLS processing
ssl_enable = yes
ssl_store_sessions = yes
ssl_store_keys = yes

# Country detection
country_detect = yes
country_detect_mysql_table = country_code
```

### 2. Advanced Configuration Options

#### High-Performance Settings
```ini
# Thread configuration
pcap_queue_receive_from_ip_process_only = no
pcap_queue_send_to_ip_process_only = no
process_rtp_packets_hash_next_thread = yes
process_rtp_packets_hash_next_thread_sem_sync = 2

# Memory optimization
heap_safe = no
heap_safe_check = no
memory_purge_interval = 60
memory_purge_if_release_gt = 500

# CPU optimization
cpu_limit_warning_mb = 80
cpu_limit_new_thread = 60
thread_affinity = yes

# Network optimization
enable_preprocess_packet = yes
enable_process_rtp_packet = yes
disable_process_packet_in_packetbuffer = no
```

#### Security Configuration
```ini
# SSL/TLS decryption
ssl_enable = yes
ssl_ipport = 443,8443,5061
ssl_store_sessions = yes
ssl_store_keys = yes
ssl_keylog_file = /var/log/voipmonitor/ssl_keylog.txt

# SRTP decryption
srtp_enable = yes
srtp_rtp_decrypt = yes
srtp_rtcp_decrypt = yes

# Fraud detection rules
fraud_detect_seq_calls = yes
fraud_detect_registers = yes
fraud_detect_concurent_calls = yes
fraud_detect_chc = yes
fraud_detect_chcr = yes
fraud_detect_d = yes
```

## Network Configuration

### 1. Network Interface Setup

#### Promiscuous Mode
```bash
# Enable promiscuous mode
ip link set eth0 promisc on

# Verify promiscuous mode
ip link show eth0
```

#### Network Mirroring Setup

##### Switch Port Mirroring
```bash
# Cisco switch configuration
configure terminal
monitor session 1 source interface gigabitethernet1/0/1 - 24
monitor session 1 destination interface gigabitethernet1/0/48
exit
```

##### Network TAP Configuration
```bash
# Configure TAP interface
ip link add name tap0 type dummy
ip link set tap0 up
ip link set tap0 promisc on

# Configure VoIPmonitor to use TAP
echo "interface = tap0" >> /etc/voipmonitor.conf
```

### 2. Firewall Configuration

#### Allow Management Interface
```bash
# UFW (Ubuntu)
ufw allow 5029/tcp

# firewalld (CentOS/RHEL)
firewall-cmd --permanent --add-port=5029/tcp
firewall-cmd --reload

# iptables
iptables -A INPUT -p tcp --dport 5029 -j ACCEPT
```

## Service Management

### 1. Systemd Service

#### Service Configuration
```ini
# /etc/systemd/system/voipmonitor.service
[Unit]
Description=VoIPmonitor network packet sniffer
After=network.target mysql.service
Wants=mysql.service

[Service]
Type=forking
User=root
Group=root
ExecStart=/usr/local/sbin/voipmonitor --config-file=/etc/voipmonitor.conf --daemon
ExecReload=/bin/kill -HUP $MAINPID
PIDFile=/var/run/voipmonitor.pid
Restart=always
RestartSec=10
LimitNOFILE=65536
LimitCORE=infinity

[Install]
WantedBy=multi-user.target
```

#### Service Management Commands
```bash
# Enable and start service
systemctl enable voipmonitor
systemctl start voipmonitor

# Check service status
systemctl status voipmonitor

# View logs
journalctl -u voipmonitor -f

# Restart service
systemctl restart voipmonitor

# Stop service
systemctl stop voipmonitor
```

### 2. Log Rotation

#### Logrotate Configuration
```bash
# /etc/logrotate.d/voipmonitor
/var/log/voipmonitor/*.log {
    daily
    rotate 30
    compress
    delaycompress
    missingok
    notifempty
    create 644 root root
    postrotate
        /bin/kill -HUP $(cat /var/run/voipmonitor.pid 2>/dev/null) 2>/dev/null || true
    endscript
}
```

## Performance Tuning

### 1. System Optimization

#### Kernel Parameters
```bash
# /etc/sysctl.d/99-voipmonitor.conf
# Network buffer sizes
net.core.rmem_max = 134217728
net.core.wmem_max = 134217728
net.core.netdev_max_backlog = 5000
net.core.netdev_budget = 600

# TCP settings
net.ipv4.tcp_rmem = 4096 87380 134217728
net.ipv4.tcp_wmem = 4096 65536 134217728
net.ipv4.tcp_congestion_control = bbr

# Memory management
vm.dirty_ratio = 15
vm.dirty_background_ratio = 5
vm.swappiness = 1

# File system
fs.file-max = 2097152

# Apply settings
sysctl -p /etc/sysctl.d/99-voipmonitor.conf
```

#### System Limits
```bash
# /etc/security/limits.d/99-voipmonitor.conf
root soft nofile 65536
root hard nofile 65536
root soft nproc 32768
root hard nproc 32768
root soft core unlimited
root hard core unlimited
```

### 2. CPU and Memory Optimization

#### CPU Affinity
```bash
# Set CPU affinity for VoIPmonitor process
taskset -cp 0-7 $(pgrep voipmonitor)

# Or configure in systemd service
echo "CPUAffinity=0-7" >> /etc/systemd/system/voipmonitor.service
```

#### NUMA Optimization
```bash
# Check NUMA topology
numactl --hardware

# Run VoIPmonitor on specific NUMA node
numactl --cpunodebind=0 --membind=0 voipmonitor --config-file=/etc/voipmonitor.conf
```

## Monitoring and Maintenance

### 1. Health Monitoring

#### System Monitoring Script
```bash
#!/bin/bash
# /usr/local/bin/voipmonitor-health-check.sh

# Check if VoIPmonitor is running
if ! pgrep -x "voipmonitor" > /dev/null; then
    echo "ERROR: VoIPmonitor is not running"
    systemctl start voipmonitor
    exit 1
fi

# Check database connection
mysql -u voipmonitor -p$MYSQL_PASSWORD -e "SELECT 1" voipmonitor > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: Cannot connect to database"
    exit 1
fi

# Check disk space
DISK_USAGE=$(df /var/spool/voipmonitor | awk 'NR==2 {print $5}' | sed 's/%//')
if [ $DISK_USAGE -gt 90 ]; then
    echo "WARNING: Disk usage is ${DISK_USAGE}%"
fi

# Check memory usage
MEM_USAGE=$(free | awk 'NR==2{printf "%.0f", $3*100/$2}')
if [ $MEM_USAGE -gt 90 ]; then
    echo "WARNING: Memory usage is ${MEM_USAGE}%"
fi

echo "VoIPmonitor health check passed"
```

#### Cron Job Setup
```bash
# Add to crontab
crontab -e

# Run health check every 5 minutes
*/5 * * * * /usr/local/bin/voipmonitor-health-check.sh >> /var/log/voipmonitor-health.log 2>&1
```

### 2. Database Maintenance

#### Partition Management
```bash
#!/bin/bash
# /usr/local/bin/manage-partitions.sh

MYSQL_USER="voipmonitor"
MYSQL_PASS="secure_password"
MYSQL_DB="voipmonitor"

# Create partition for tomorrow
TOMORROW=$(date -d "+1 day" +%Y-%m-%d)
mysql -u $MYSQL_USER -p$MYSQL_PASS $MYSQL_DB << EOF
ALTER TABLE cdr ADD PARTITION IF NOT EXISTS (
    PARTITION p_$(echo $TOMORROW | sed 's/-/_/g') 
    VALUES LESS THAN (TO_DAYS('$(date -d "$TOMORROW +1 day" +%Y-%m-%d)'))
);
EOF

# Remove partitions older than 365 days
OLD_DATE=$(date -d "-365 days" +%Y_%m_%d)
mysql -u $MYSQL_USER -p$MYSQL_PASS $MYSQL_DB << EOF
ALTER TABLE cdr DROP PARTITION IF EXISTS p_$OLD_DATE;
EOF
```

## Troubleshooting

### 1. Common Issues

#### Permission Issues
```bash
# Fix file permissions
chown -R root:root /etc/voipmonitor/
chmod 640 /etc/voipmonitor.conf
chown -R voipmonitor:voipmonitor /var/spool/voipmonitor/
chmod 755 /var/spool/voipmonitor/
```

#### Network Interface Issues
```bash
# Check interface status
ip link show

# Verify packet capture
tcpdump -i eth0 -c 10

# Check VoIPmonitor interface binding
netstat -tulpn | grep voipmonitor
```

#### Database Connection Issues
```bash
# Test database connection
mysql -u voipmonitor -p -h localhost voipmonitor

# Check MySQL service
systemctl status mysql

# Review MySQL error log
tail -f /var/log/mysql/error.log
```

### 2. Performance Issues

#### High CPU Usage
```bash
# Check CPU usage by process
top -p $(pgrep voipmonitor)

# Analyze system calls
strace -p $(pgrep voipmonitor) -c

# Check interrupt distribution
cat /proc/interrupts
```

#### Memory Issues
```bash
# Check memory usage
cat /proc/$(pgrep voipmonitor)/status | grep -E "VmRSS|VmSize"

# Monitor memory leaks
valgrind --tool=memcheck --leak-check=full voipmonitor --config-file=/etc/voipmonitor.conf
```

#### Disk I/O Issues
```bash
# Monitor disk I/O
iotop -p $(pgrep voipmonitor)

# Check disk space
df -h /var/spool/voipmonitor/

# Analyze I/O patterns
iostat -x 1
```

This deployment guide provides comprehensive instructions for installing, configuring, and maintaining VoIPmonitor in production environments.