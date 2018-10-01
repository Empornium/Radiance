/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8 */;
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;

--
-- Table structure for table `torrents`
--

/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE IF NOT EXISTS `torrents` (
  `ID` int(10) NOT NULL AUTO_INCREMENT,
  `GroupID` int(10) NOT NULL,
  `UserID` int(10) DEFAULT NULL,
  `info_hash` blob NOT NULL,
  `InfoHash` char(40) NOT NULL DEFAULT '',
  `FileCount` int(6) NOT NULL,
  `FileList` mediumtext NOT NULL,
  `FilePath` varchar(255) NOT NULL DEFAULT '',
  `Size` bigint(12) NOT NULL,
  `Leechers` int(6) NOT NULL DEFAULT '0',
  `Seeders` int(6) NOT NULL DEFAULT '0',
  `AverageSeeders` float NOT NULL DEFAULT '0',
  `last_action` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `FreeTorrent` enum('0','1','2') NOT NULL DEFAULT '0',
  `DoubleTorrent` enum('0','1') NOT NULL DEFAULT '0',
  `Time` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `Anonymous` enum('0','1') NOT NULL DEFAULT '0',
  `Thanks` text NOT NULL,
  `Snatched` int(10) unsigned NOT NULL DEFAULT '0',
  `balance` bigint(20) NOT NULL DEFAULT '0',
  `LastLogged` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `pid` int(5) NOT NULL DEFAULT '0',
  `LastReseedRequest` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `ExtendedGrace` enum('0','1') NOT NULL DEFAULT '0',
  `Tasted` enum('0','1') NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`),
  UNIQUE KEY `InfoHash` (`info_hash`(40)),
  KEY `GroupID` (`GroupID`),
  KEY `UserID` (`UserID`),
  KEY `FileCount` (`FileCount`),
  KEY `Size` (`Size`),
  KEY `Seeders` (`Seeders`),
  KEY `Leechers` (`Leechers`),
  KEY `Snatched` (`Snatched`),
  KEY `last_action` (`last_action`),
  KEY `Time` (`Time`),
  KEY `LastLogged` (`LastLogged`),
  KEY `FreeTorrent` (`FreeTorrent`),
  KEY `AverageSeeders` (`AverageSeeders`)
) ENGINE=InnoDB AUTO_INCREMENT=421616 DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `users_slots`
--

/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE IF NOT EXISTS `users_slots` (
  `UserID` int(11) NOT NULL,
  `TorrentID` int(11) NOT NULL,
  `FreeLeech` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `DoubleSeed` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  PRIMARY KEY (`UserID`,`TorrentID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `users_freeleeches`
--

/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE IF NOT EXISTS `users_freeleeches` (
  `UserID` int(11) NOT NULL,
  `TorrentID` int(11) NOT NULL,
  `Downloaded` bigint(20) unsigned NOT NULL DEFAULT '0',
  `Uploaded` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`UserID`,`TorrentID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `users_main`
--

/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE IF NOT EXISTS `users_main` (
  `ID` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `Username` varchar(30) NOT NULL,
  `Email` varchar(255) NOT NULL,
  `PassHash` char(40) NOT NULL,
  `Secret` char(32) NOT NULL,
  `TorrentKey` char(32) NOT NULL,
  `IRCKey` char(32) DEFAULT NULL,
  `LastLogin` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `LastAccess` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `IP` varchar(15) NOT NULL DEFAULT '0.0.0.0',
  `Uploaded` bigint(20) unsigned NOT NULL DEFAULT '0',
  `Downloaded` bigint(20) unsigned NOT NULL DEFAULT '0',
  `UploadedDaily` bigint(20) unsigned NOT NULL DEFAULT '0',
  `DownloadedDaily` bigint(20) unsigned NOT NULL DEFAULT '0',
  `Title` varchar(128) NOT NULL,
  `Enabled` enum('0','1','2') NOT NULL DEFAULT '0',
  `Paranoia` text,
  `Visible` enum('1','0') NOT NULL DEFAULT '1',
  `Invites` int(10) unsigned NOT NULL DEFAULT '0',
  `PermissionID` int(10) unsigned NOT NULL,
  `GroupPermissionID` int(10) unsigned NOT NULL DEFAULT '0',
  `CustomPermissions` text,
  `LastSeed` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `can_leech` tinyint(4) NOT NULL DEFAULT '1',
  `track_ipv6` enum('1','0') NOT NULL DEFAULT '0',
  `wait_time` int(11) NOT NULL,
  `peers_limit` int(11) DEFAULT '1000',
  `torrents_limit` int(11) DEFAULT '1000',
  `torrent_pass` char(32) NOT NULL,
  `OldPassHash` char(32) DEFAULT NULL,
  `Cursed` enum('1','0') NOT NULL DEFAULT '0',
  `CookieID` varchar(32) DEFAULT NULL,
  `RequiredRatio` double(10,8) NOT NULL DEFAULT '0.00000000',
  `RequiredRatioWork` double(10,8) NOT NULL DEFAULT '0.00000000',
  `Language` char(2) NOT NULL DEFAULT '',
  `ipcc` char(2) NOT NULL DEFAULT '',
  `FLTokens` int(10) NOT NULL DEFAULT '0',
  `personal_freeleech` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `personal_doubleseed` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `SeedHoursDaily` double(11,2) NOT NULL DEFAULT '0.00',
  `SeedHours` double(11,2) NOT NULL DEFAULT '0.00',
  `CreditsDaily` double(11,2) NOT NULL DEFAULT '0.00',
  `Credits` double(11,2) NOT NULL DEFAULT '0.00',
  `Signature` text,
  `Flag` varchar(50) NOT NULL DEFAULT '',
  PRIMARY KEY (`ID`),
  UNIQUE KEY `Username` (`Username`),
  KEY `Email` (`Email`),
  KEY `PassHash` (`PassHash`),
  KEY `LastAccess` (`LastAccess`),
  KEY `IP` (`IP`),
  KEY `Uploaded` (`Uploaded`),
  KEY `Downloaded` (`Downloaded`),
  KEY `Enabled` (`Enabled`),
  KEY `Invites` (`Invites`),
  KEY `torrent_pass` (`torrent_pass`),
  KEY `RequiredRatio` (`RequiredRatio`),
  KEY `SeedHoursDaily` (`SeedHoursDaily`)
) ENGINE=InnoDB AUTO_INCREMENT=6582 DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `xbt_client_blacklist`
--

/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE IF NOT EXISTS `xbt_client_blacklist` (
  `id` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `peer_id` varchar(20) DEFAULT NULL,
  `vstring` varchar(200) DEFAULT '',
  PRIMARY KEY (`id`),
  UNIQUE KEY `peer_id` (`peer_id`)
) ENGINE=InnoDB AUTO_INCREMENT=4 DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `xbt_files_users`
--

/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE IF NOT EXISTS `xbt_files_users` (
  `uid` int(11) NOT NULL,
  `active` tinyint(1) NOT NULL,
  `announced` int(11) NOT NULL,
  `completed` tinyint(1) NOT NULL DEFAULT '0',
  `downloaded` bigint(20) NOT NULL,
  `remaining` bigint(20) NOT NULL,
  `uploaded` bigint(20) NOT NULL,
  `upspeed` bigint(20) NOT NULL,
  `downspeed` bigint(20) NOT NULL,
  `corrupt` bigint(20) NOT NULL DEFAULT '0',
  `timespent` bigint(20) NOT NULL,
  `useragent` varchar(51) NOT NULL,
  `connectable` tinyint(4) NOT NULL DEFAULT '1',
  `peer_id` binary(20) NOT NULL DEFAULT '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0',
  `fid` int(11) NOT NULL,
  `ctime` int(11) DEFAULT NULL,
  `mtime` int(11) NOT NULL,
  `ip` varchar(15) NOT NULL DEFAULT '',
  `ipv4` varbinary(4) DEFAULT NULL,
  `ipv6` varbinary(16) DEFAULT NULL,
  `port` int(6) NOT NULL DEFAULT '0',
  `domain_id` tinyint(3) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`peer_id`,`fid`,`uid`),
  KEY `remaining_idx` (`remaining`),
  KEY `fid_idx` (`fid`),
  KEY `mtime_idx` (`mtime`),
  KEY `uid_active` (`uid`,`active`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `xbt_snatched`
--

/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE IF NOT EXISTS `xbt_snatched` (
  `uid` int(11) NOT NULL DEFAULT '0',
  `tstamp` int(11) NOT NULL,
  `fid` int(11) NOT NULL,
  `ipv4` varbinary(4) DEFAULT NULL,
  `ipv6` varbinary(16) DEFAULT NULL,
  KEY `fid` (`fid`),
  KEY `uid` (`uid`),
  KEY `tstamp` (`tstamp`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `xbt_peers_history`
--

/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE IF NOT EXISTS `xbt_peers_history` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `uid` int(11) NOT NULL,
  `downloaded` bigint(20) NOT NULL,
  `remaining` bigint(20) NOT NULL,
  `uploaded` bigint(20) NOT NULL,
  `upspeed` bigint(20) NOT NULL,
  `downspeed` bigint(20) NOT NULL,
  `timespent` bigint(20) NOT NULL,
  `peer_id` binary(20) NOT NULL DEFAULT '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0',
  `ipv4` varbinary(4) DEFAULT NULL,
  `ipv6` varbinary(16) DEFAULT NULL,
  `fid` int(11) NOT NULL,
  `mtime` int(11) NOT NULL,
  PRIMARY KEY (`id`),
  KEY `uid` (`uid`),
  KEY `fid` (`fid`),
  KEY `upspeed` (`upspeed`),
  KEY `mtime` (`mtime`)
) ENGINE=InnoDB AUTO_INCREMENT=3923468 DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `options`
--

/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE IF NOT EXISTS `options` (
  `Name` varchar(255) NOT NULL,
  `Value` text NOT NULL,
  PRIMARY KEY (`Name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;

-- Dump completed on 2017-06-19 22:53:08
