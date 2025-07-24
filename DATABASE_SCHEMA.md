# VoIPmonitor Database Schema Documentation

## Database Overview

VoIPmonitor uses MySQL/MariaDB as its primary database backend with a sophisticated schema designed for high-performance VoIP monitoring and analysis.

### Key Features
- **Time-based Partitioning**: Tables partitioned by date for performance
- **Indexing Strategy**: Optimized indexes for common queries
- **Data Retention**: Automated cleanup of old data
- **Replication Support**: Master-slave replication capability

## Core Tables

### 1. CDR (Call Detail Records) - `cdr`

The main table storing call information and quality metrics.

```sql
CREATE TABLE `cdr` (
  `ID` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `calldate` datetime NOT NULL,
  `callend` datetime NOT NULL,
  `duration` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `connect_duration` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `progress_time` smallint(5) unsigned NOT NULL DEFAULT '0',
  `first_rtp_time` smallint(5) unsigned NOT NULL DEFAULT '0',
  `caller` varchar(255) NOT NULL DEFAULT '',
  `called` varchar(255) NOT NULL DEFAULT '',
  `caller_domain` varchar(255) NOT NULL DEFAULT '',
  `called_domain` varchar(255) NOT NULL DEFAULT '',
  `callername` varchar(255) NOT NULL DEFAULT '',
  `caller_id` varchar(255) NOT NULL DEFAULT '',
  `called_id` varchar(255) NOT NULL DEFAULT '',
  `lastSIPresponse_id` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `lastSIPresponseNum` smallint(5) unsigned NOT NULL DEFAULT '0',
  `bye` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `lastSIPresponse` varchar(255) NOT NULL DEFAULT '',
  `reason_sip_id` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `reason_sip_text` varchar(255) NOT NULL DEFAULT '',
  `reason_q850_id` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `reason_q850_text` varchar(255) NOT NULL DEFAULT '',
  `a_index` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `b_index` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `a_payload` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_payload` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_saddr` int(10) unsigned NOT NULL DEFAULT '0',
  `b_saddr` int(10) unsigned NOT NULL DEFAULT '0',
  `a_daddr` int(10) unsigned NOT NULL DEFAULT '0',
  `b_daddr` int(10) unsigned NOT NULL DEFAULT '0',
  `a_sport` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_sport` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_dport` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_dport` smallint(5) unsigned NOT NULL DEFAULT '0',
  `whohanged` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `a_ua_id` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `b_ua_id` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `a_ua` varchar(255) NOT NULL DEFAULT '',
  `b_ua` varchar(255) NOT NULL DEFAULT '',
  `a_avgjitter_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_maxjitter_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_avgjitter_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_maxjitter_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_sl1` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_sl2` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_sl3` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_sl4` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_sl5` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_sl6` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_sl7` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_sl8` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_sl9` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_sl10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_d50` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_d70` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_d90` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_d120` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_d150` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_d200` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_d300` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_sl1` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_sl2` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_sl3` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_sl4` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_sl5` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_sl6` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_sl7` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_sl8` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_sl9` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_sl10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_d50` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_d70` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_d90` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_d120` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_d150` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_d200` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_d300` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_mos_lqo_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `b_mos_lqo_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `a_mos_f1_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `a_mos_f2_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `a_mos_adapt_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `b_mos_f1_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `b_mos_f2_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `b_mos_adapt_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `a_rtcp_loss` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `a_rtcp_maxfr` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `a_rtcp_avgfr_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `a_rtcp_maxjitter` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_rtcp_avgjitter_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_rtcp_loss` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `b_rtcp_maxfr` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `b_rtcp_avgfr_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `b_rtcp_maxjitter` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_rtcp_avgjitter_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_last_rtp_from_end` smallint(5) unsigned NOT NULL DEFAULT '0',
  `b_last_rtp_from_end` smallint(5) unsigned NOT NULL DEFAULT '0',
  `a_rtp_ptime` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `b_rtp_ptime` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `payload` smallint(5) unsigned NOT NULL DEFAULT '0',
  `jitter_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `mos_min_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `a_mos_min_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `b_mos_min_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `packet_loss_perc_mult1000` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `a_packet_loss_perc_mult1000` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `b_packet_loss_perc_mult1000` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `delay_sum` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `a_delay_sum` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `b_delay_sum` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `delay_avg_mult100` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `a_delay_avg_mult100` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `b_delay_avg_mult100` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `delay_cnt` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `a_delay_cnt` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `b_delay_cnt` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `rtcp_avgfr_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `rtcp_avgjitter_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `lost` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `a_lost` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `b_lost` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `caller_codec` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `called_codec` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `caller_hold` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `called_hold` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `flags` bigint(20) unsigned NOT NULL DEFAULT '0',
  `cdr_flags` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`calldate`),
  KEY `calldate` (`calldate`),
  KEY `callend` (`callend`),
  KEY `duration` (`duration`),
  KEY `caller` (`caller`),
  KEY `called` (`called`),
  KEY `callername` (`callername`),
  KEY `lastSIPresponseNum` (`lastSIPresponseNum`),
  KEY `bye` (`bye`),
  KEY `a_saddr` (`a_saddr`),
  KEY `b_saddr` (`b_saddr`),
  KEY `a_lost` (`a_lost`),
  KEY `b_lost` (`b_lost`),
  KEY `a_maxjitter_mult10` (`a_maxjitter_mult10`),
  KEY `b_maxjitter_mult10` (`b_maxjitter_mult10`),
  KEY `a_rtcp_loss` (`a_rtcp_loss`),
  KEY `b_rtcp_loss` (`b_rtcp_loss`)
) ENGINE=InnoDB
PARTITION BY RANGE (to_days(calldate))
(PARTITION p_2024_01_01 VALUES LESS THAN (to_days('2024-01-02')) ENGINE = InnoDB,
 PARTITION p_2024_01_02 VALUES LESS THAN (to_days('2024-01-03')) ENGINE = InnoDB);
```

### 2. SIP Messages - `message`

Stores all SIP messages for detailed protocol analysis.

```sql
CREATE TABLE `message` (
  `ID` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `id_sensor` smallint(5) unsigned NOT NULL DEFAULT '0',
  `fname` bigint(20) unsigned NOT NULL DEFAULT '0',
  `calldate` datetime NOT NULL,
  `caller` varchar(255) NOT NULL DEFAULT '',
  `called` varchar(255) NOT NULL DEFAULT '',
  `caller_domain` varchar(255) NOT NULL DEFAULT '',
  `called_domain` varchar(255) NOT NULL DEFAULT '',
  `callername` varchar(255) NOT NULL DEFAULT '',
  `a_ua` varchar(255) NOT NULL DEFAULT '',
  `b_ua` varchar(255) NOT NULL DEFAULT '',
  `sipcallerip` int(10) unsigned NOT NULL DEFAULT '0',
  `sipcalledip` int(10) unsigned NOT NULL DEFAULT '0',
  `message` text,
  `contenttype` varchar(255) NOT NULL DEFAULT '',
  `response_num` smallint(5) unsigned NOT NULL DEFAULT '0',
  `response_time` smallint(5) unsigned NOT NULL DEFAULT '0',
  `response_string` varchar(255) NOT NULL DEFAULT '',
  `content_length` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `bye` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `lastSIPresponse_id` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `lastSIPresponseNum` smallint(5) unsigned NOT NULL DEFAULT '0',
  `flags` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`calldate`),
  KEY `calldate` (`calldate`),
  KEY `caller` (`caller`),
  KEY `called` (`called`),
  KEY `sipcallerip` (`sipcallerip`),
  KEY `sipcalledip` (`sipcalledip`),
  KEY `lastSIPresponseNum` (`lastSIPresponseNum`),
  KEY `contenttype` (`contenttype`)
) ENGINE=InnoDB
PARTITION BY RANGE (to_days(calldate));
```

### 3. RTP Statistics - `rtp_stat`

Detailed RTP stream statistics for quality analysis.

```sql
CREATE TABLE `rtp_stat` (
  `ID` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `id_sensor` smallint(5) unsigned NOT NULL DEFAULT '0',
  `calldate` datetime NOT NULL,
  `saddr` int(10) unsigned NOT NULL DEFAULT '0',
  `daddr` int(10) unsigned NOT NULL DEFAULT '0',
  `sport` smallint(5) unsigned NOT NULL DEFAULT '0',
  `dport` smallint(5) unsigned NOT NULL DEFAULT '0',
  `ssrc` int(10) unsigned NOT NULL DEFAULT '0',
  `received` int(10) unsigned NOT NULL DEFAULT '0',
  `lost` int(10) unsigned NOT NULL DEFAULT '0',
  `packet_loss_perc_mult1000` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `avgjitter_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `maxjitter_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `sl1` smallint(5) unsigned NOT NULL DEFAULT '0',
  `sl2` smallint(5) unsigned NOT NULL DEFAULT '0',
  `sl3` smallint(5) unsigned NOT NULL DEFAULT '0',
  `sl4` smallint(5) unsigned NOT NULL DEFAULT '0',
  `sl5` smallint(5) unsigned NOT NULL DEFAULT '0',
  `sl6` smallint(5) unsigned NOT NULL DEFAULT '0',
  `sl7` smallint(5) unsigned NOT NULL DEFAULT '0',
  `sl8` smallint(5) unsigned NOT NULL DEFAULT '0',
  `sl9` smallint(5) unsigned NOT NULL DEFAULT '0',
  `sl10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `d50` smallint(5) unsigned NOT NULL DEFAULT '0',
  `d70` smallint(5) unsigned NOT NULL DEFAULT '0',
  `d90` smallint(5) unsigned NOT NULL DEFAULT '0',
  `d120` smallint(5) unsigned NOT NULL DEFAULT '0',
  `d150` smallint(5) unsigned NOT NULL DEFAULT '0',
  `d200` smallint(5) unsigned NOT NULL DEFAULT '0',
  `d300` smallint(5) unsigned NOT NULL DEFAULT '0',
  `mos_f1_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `mos_f2_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `mos_adapt_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `mos_lqo_mult10` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `payload` smallint(5) unsigned NOT NULL DEFAULT '0',
  `jitter_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `delay_sum` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `delay_avg_mult100` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `delay_cnt` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `rtcp_avgfr_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `rtcp_avgjitter_mult10` smallint(5) unsigned NOT NULL DEFAULT '0',
  `flags` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`calldate`),
  KEY `calldate` (`calldate`),
  KEY `saddr` (`saddr`),
  KEY `daddr` (`daddr`),
  KEY `ssrc` (`ssrc`),
  KEY `lost` (`lost`),
  KEY `maxjitter_mult10` (`maxjitter_mult10`),
  KEY `mos_lqo_mult10` (`mos_lqo_mult10`)
) ENGINE=InnoDB
PARTITION BY RANGE (to_days(calldate));
```

### 4. Registration Table - `register_state`

SIP registration tracking and monitoring.

```sql
CREATE TABLE `register_state` (
  `ID` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `id_sensor` smallint(5) unsigned NOT NULL DEFAULT '0',
  `fname` bigint(20) unsigned NOT NULL DEFAULT '0',
  `created_at` datetime NOT NULL,
  `sipcallerip` int(10) unsigned NOT NULL DEFAULT '0',
  `sipcalledip` int(10) unsigned NOT NULL DEFAULT '0',
  `from_num` varchar(255) NOT NULL DEFAULT '',
  `from_name` varchar(255) NOT NULL DEFAULT '',
  `from_domain` varchar(255) NOT NULL DEFAULT '',
  `to_num` varchar(255) NOT NULL DEFAULT '',
  `to_domain` varchar(255) NOT NULL DEFAULT '',
  `contact_num` varchar(255) NOT NULL DEFAULT '',
  `contact_domain` varchar(255) NOT NULL DEFAULT '',
  `digestusername` varchar(255) NOT NULL DEFAULT '',
  `digestrealm` varchar(255) NOT NULL DEFAULT '',
  `expires` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `expires_at` datetime DEFAULT NULL,
  `state` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `ua_id` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `ua` varchar(255) NOT NULL DEFAULT '',
  `flags` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`),
  KEY `created_at` (`created_at`),
  KEY `sipcallerip` (`sipcallerip`),
  KEY `from_num` (`from_num`),
  KEY `to_num` (`to_num`),
  KEY `contact_num` (`contact_num`),
  KEY `expires_at` (`expires_at`),
  KEY `state` (`state`)
) ENGINE=InnoDB;
```

### 5. Fraud Detection - `fraud_alert`

Security and fraud detection events.

```sql
CREATE TABLE `fraud_alert` (
  `ID` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `id_sensor` smallint(5) unsigned NOT NULL DEFAULT '0',
  `alert_info_id` int(10) unsigned NOT NULL DEFAULT '0',
  `at` datetime NOT NULL,
  `type_info_id` int(10) unsigned NOT NULL DEFAULT '0',
  `country_code` varchar(5) NOT NULL DEFAULT '',
  `country_code_2` varchar(5) NOT NULL DEFAULT '',
  `descr` text,
  `alert_info` text,
  `sipcallerip` int(10) unsigned NOT NULL DEFAULT '0',
  `sipcalledip` int(10) unsigned NOT NULL DEFAULT '0',
  `caller` varchar(255) NOT NULL DEFAULT '',
  `called` varchar(255) NOT NULL DEFAULT '',
  `caller_domain` varchar(255) NOT NULL DEFAULT '',
  `called_domain` varchar(255) NOT NULL DEFAULT '',
  `callername` varchar(255) NOT NULL DEFAULT '',
  `ua_id` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `ua` varchar(255) NOT NULL DEFAULT '',
  `flags` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`),
  KEY `at` (`at`),
  KEY `alert_info_id` (`alert_info_id`),
  KEY `type_info_id` (`type_info_id`),
  KEY `sipcallerip` (`sipcallerip`),
  KEY `caller` (`caller`),
  KEY `called` (`called`)
) ENGINE=InnoDB;
```

## Support Tables

### User Agent Strings - `cdr_ua`
```sql
CREATE TABLE `cdr_ua` (
  `id` mediumint(8) unsigned NOT NULL AUTO_INCREMENT,
  `ua` varchar(512) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  UNIQUE KEY `ua` (`ua`)
) ENGINE=InnoDB;
```

### SIP Response Codes - `cdr_sip_response`
```sql
CREATE TABLE `cdr_sip_response` (
  `id` mediumint(8) unsigned NOT NULL AUTO_INCREMENT,
  `lastSIPresponse` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  UNIQUE KEY `lastSIPresponse` (`lastSIPresponse`)
) ENGINE=InnoDB;
```

### Countries - `country_code`
```sql
CREATE TABLE `country_code` (
  `country_code` varchar(5) NOT NULL DEFAULT '',
  `country_name` varchar(255) NOT NULL DEFAULT '',
  `continent` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`country_code`)
) ENGINE=InnoDB;
```

## Partitioning Strategy

### Daily Partitioning
VoIPmonitor uses daily partitioning for high-volume tables:

```sql
-- Example partition management
ALTER TABLE cdr ADD PARTITION (
  PARTITION p_2024_01_15 VALUES LESS THAN (to_days('2024-01-16'))
);

-- Drop old partitions
ALTER TABLE cdr DROP PARTITION p_2023_01_01;
```

### Partition Maintenance Script
```bash
#!/bin/bash
# Automatic partition management
mysql -e "CALL create_partition_if_not_exists('cdr', '$(date +%Y-%m-%d)');"
mysql -e "CALL drop_old_partitions('cdr', 90);" # Keep 90 days
```

## Indexing Strategy

### Performance Indexes
```sql
-- CDR table performance indexes
CREATE INDEX idx_cdr_caller_calldate ON cdr (caller, calldate);
CREATE INDEX idx_cdr_called_calldate ON cdr (called, calldate);
CREATE INDEX idx_cdr_sipcallerip_calldate ON cdr (a_saddr, calldate);
CREATE INDEX idx_cdr_quality ON cdr (a_mos_lqo_mult10, b_mos_lqo_mult10);

-- Message table indexes
CREATE INDEX idx_message_caller_calldate ON message (caller, calldate);
CREATE INDEX idx_message_response_num ON message (response_num, calldate);

-- RTP statistics indexes
CREATE INDEX idx_rtp_stat_quality ON rtp_stat (mos_lqo_mult10, calldate);
CREATE INDEX idx_rtp_stat_loss ON rtp_stat (lost, calldate);
```

## Data Retention Policy

### Automated Cleanup
```sql
-- Cleanup procedure example
DELIMITER $$
CREATE PROCEDURE cleanup_old_data(IN table_name VARCHAR(64), IN days_to_keep INT)
BEGIN
    SET @sql = CONCAT('DELETE FROM ', table_name, 
                      ' WHERE calldate < DATE_SUB(NOW(), INTERVAL ', days_to_keep, ' DAY)');
    PREPARE stmt FROM @sql;
    EXECUTE stmt;
    DEALLOCATE PREPARE stmt;
END$$
DELIMITER ;

-- Schedule cleanup
-- CDR: Keep 1 year
CALL cleanup_old_data('cdr', 365);
-- Messages: Keep 6 months  
CALL cleanup_old_data('message', 180);
-- RTP stats: Keep 3 months
CALL cleanup_old_data('rtp_stat', 90);
```

## Query Examples

### Common Queries

#### Call Quality Analysis
```sql
-- Calls with poor quality (MOS < 3.0)
SELECT caller, called, calldate, 
       a_mos_lqo_mult10/10.0 as caller_mos,
       b_mos_lqo_mult10/10.0 as called_mos
FROM cdr 
WHERE calldate >= '2024-01-01'
  AND (a_mos_lqo_mult10 < 30 OR b_mos_lqo_mult10 < 30)
  AND a_mos_lqo_mult10 > 0
ORDER BY calldate DESC;
```

#### High Packet Loss Calls
```sql
-- Calls with packet loss > 5%
SELECT caller, called, calldate, duration,
       a_packet_loss_perc_mult1000/1000.0 as caller_loss_pct,
       b_packet_loss_perc_mult1000/1000.0 as called_loss_pct
FROM cdr 
WHERE calldate >= '2024-01-01'
  AND (a_packet_loss_perc_mult1000 > 5000 OR b_packet_loss_perc_mult1000 > 5000)
ORDER BY calldate DESC;
```

#### Registration Analysis
```sql
-- Failed registrations
SELECT from_num, from_domain, sipcallerip, created_at, ua
FROM register_state 
WHERE state = 2  -- Failed state
  AND created_at >= '2024-01-01'
ORDER BY created_at DESC;
```

#### Fraud Detection Summary
```sql
-- Fraud alerts by type
SELECT type_info_id, COUNT(*) as alert_count,
       MIN(at) as first_alert, MAX(at) as last_alert
FROM fraud_alert 
WHERE at >= '2024-01-01'
GROUP BY type_info_id
ORDER BY alert_count DESC;
```

## Performance Tuning

### MySQL Configuration
```ini
[mysqld]
# Memory settings
innodb_buffer_pool_size = 4G
innodb_log_file_size = 512M
innodb_log_buffer_size = 64M

# Performance settings
innodb_flush_log_at_trx_commit = 2
innodb_file_per_table = 1
innodb_thread_concurrency = 0

# Partitioning support
partition = ON
```

### Monitoring Queries
```sql
-- Check partition sizes
SELECT 
    table_name, 
    partition_name, 
    table_rows, 
    data_length/1024/1024 as data_mb
FROM information_schema.partitions 
WHERE table_schema = 'voipmonitor' 
  AND table_name = 'cdr'
ORDER BY partition_ordinal_position;

-- Index usage statistics
SELECT 
    table_name, 
    index_name, 
    cardinality,
    seq_in_index
FROM information_schema.statistics 
WHERE table_schema = 'voipmonitor'
ORDER BY table_name, seq_in_index;
```

This database schema supports high-volume VoIP monitoring with optimized performance for real-time analysis and historical reporting.