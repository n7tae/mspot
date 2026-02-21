<!DOCTYPE html>
<?php
$inidata = parse_ini_file('/home/USER/mspot/mspot.ini', true);
if (false === $inidata)
	die('Could not parse mspot ini file!');

// here are some useful defines for html
$table = '<table cellpadding="1" border="1" style="font-family: monospace">';
$td = '<td style="text-align:center">';

$dbfile = $inidata['Gateway']['DBPath'];

function GetIP(string $type)
{
	if ('internal' == $type) {
		$iplist = explode(' ', `hostname -I`);
		foreach ($iplist as $ip) {
			if (strpos($ip, '.')) break;
		}
	} else if ('ipv6' == $type)
		$ip = trim(`dig @resolver1.ipv6-sandbox.opendns.com AAAA myip.opendns.com +short -6`);
	else if ('ipv4' == $type)
		$ip = trim(`dig @resolver4.opendns.com myip.opendns.com +short -4`);
	else
		$ip = '';
	return $ip;
}

function SecToString(int $sec) {
	if ($sec >= 86400)
		return sprintf("%0.2f days", $sec/86400);
	$hrs = intdiv($sec, 3600);
	$sec %= 3600;
	$min = intdiv($sec, 60);
	$sec %= 60;
	if ($hrs) return sprintf("%2d hr  %2d min", $hrs, $min);
	if ($min) return sprintf("%2d min %2d sec", $min, $sec);
	return sprintf("%2d sec", $sec);
}

function SrcLinkToQRZ(string $src)
{
	$cs = $src;
	foreach(array(' ', '-', '/', '.') as $chr) {
		if (str_contains($cs, $chr))
			$cs = strstr($cs, $chr, true);
	}
	$link = '<a*target="_blank"*href="https://www.qrz.com/db/'.$cs.'">'.$src.'</a>';
	$len = strlen($src);
	while ($len < 10) {
		$link .= ' ';
		$len += 1;
	}
	return $link;
}
//example URL: https://www.google.com/maps?q=+52.37745,+001.99960
function Maidenhead(string $maid, float $lat, float $lon)
{
	$str = trim($maid);
	if (6 > strlen($str))
		return $maid;
	if ($lat >= 0.0)
		$slat = '+'.$lat;
	else
		$slat = $lat;
	if ($lon >= 0.0)
		$slon = '+'.$lon;
	else
		$slon = $lon;
	$str = '<a*target="_blank"*href="https://www.google.com/maps?q='.$slat.','.$slon.'">'.$maid.'</a>';
	return $str;
}

$cfg = parse_ini_file($inifile, true);
if ($cfg === false)
	die('Could not parse '.$inifile);
?>

<html>
<head>
<title><i>mspot</i> Dashboard</title>
<meta http-equiv="refresh" content="<?php echo $cfg['Dashboard']['RefreshPeriod'];?>">
</head>
<body>
<h2><i>mspot</i> <?php $cfg['Reflector']['Callsign']; ?> Dashboard</h2>

<?php
$showorder = $cfg['Reflector']['ShowOrder'];
$showlist = explode(',', trim($showorder));
foreach($showlist as $section) {
	switch ($section) {
		case 'PS':
			echo 'Processes:<br><code>'.PHP_EOL;
			$lines = explode("\n", `ps -aux | grep -e USER -e mspot | grep -v -e grep -e journal`);
			foreach ($lines as $line) {
				echo str_replace(' ', '&nbsp;', $line), "<br>\n";
			}
			echo '</code>'.PHP_EOL;
			break;
		case 'SY':
			echo 'System Info:<br>'.PHP_EOL;
			$hn = trim(`uname -n`);
			$kn = trim(`uname -rmo`);
			$osinfo = file('/etc/os-release', FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
			foreach ($osinfo as $line) {
				list( $key, $value ) = explode('=', $line);
				if ($key == 'PRETTY_NAME') {
					$os = trim($value, '"');
					break;
				}
			}
			$cu = trim(`cat /proc/cpuinfo | grep Model`);
			if (0 == strlen($cu))
				$cu = trim(`cat /proc/cpuinfo | grep "model name"`);
			$culist = explode("\n", $cu);
			$mnlist = explode(':', $culist[0]);
			$cu = trim($mnlist[1]);
			if (count($culist) > 1)
				$cu .= ' ' . count($culist) . ' Threads';
			$cu .= ' '.str_replace("'", '&deg;', trim(`vcgencmd measure_temp`));
			echo $table.PHP_EOL;
			echo '<tr>'.$td.'CPU</td>'.$td.'Kernel</td>'.$td.'OS</td>'.$td.'Hostname</td></tr>'.PHP_EOL;
			echo '<tr>'.$td.$cu.'</td>'.$td.$kn.'</td>'.$td.$os.'</td>'.$td.$hn.'</td></tr></table><br>'.PHP_EOL;
			break;
		case 'LH':
			echo 'Last Heard:<br><code>'.PHP_EOL;
			$rstr = 'SRC     DST     Type    Via    Maidenhead   Time<br>';
			echo str_replace(' ', '&nbsp;', $rstr).PHP_EOL;
			$db = new SQLite3($dbfile, SQLITE3_OPEN_READONLY);
			//             0   1   2      3          4        5           6       7
			$ss = 'SELECT src,dst,mode,reflector,maidenhead,latitude,longitude,strftime("%s","now")-lasttime FROM lastheard ORDER BY 7 LIMIT '.$cfg['Dashboard']['LastHeardSize'].' ';
			if ($stmnt = $db->prepare($ss)) {
				if ($result = $stmnt->execute()) {
					while ($row = $result->FetchArray(SQLITE3_NUM)) {
						$rstr = SrcLinkToQRZ($row[0]).' '.$row[1].' '.$row[2].'  '.$row[3].' '.Maidenhead($row[4], $row[5], $row[6]).' '.SecToString(intval($row[7])).'<br>';
						echo str_replace('*', ' ', str_replace(' ', '&nbsp;', $rstr)).PHP_EOL;
					}
					$result->finalize();
				}
				$stmnt->close();
			}
			$db->Close();
			echo '</code><br>'.PHP_EOL;
			break;
		case 'IP':
			$hasv6 = $cfg['Gateway']['ipv6'];
			$hasv4 = $cfg['Gateway']['ipv4'];
			echo 'IP Addresses:<br>'.PHP_EOL;
			echo $table.PHP_EOL;
			echo '<tr>'.$td.'Internal</td>';
			if ($hasv4) echo $td.'IPV4</td>';
			if ($hasv6) echo $td.'IPV6</td>';
			echo '</tr>'.PHP_EOL;
			echo '<tr>'.$td.GetIP('internal').'</td>';
			if ($hasv4) echo $td.GetIP('ipv4').'</td>';
			if ($hasv6) echo $td.GetIP('ipv6').'</td>';
			echo '</tr></table><br>'.PHP_EOL;
			break;
		case 'LS':
			echo '<i>mspot</i> Status:';
			$db = new SQLite3($cfg['Gateway']['DBPath'], SQLITE3_OPEN_READONLY);
			$linkcount = $db->querySingle('SELECT COUNT(*) FROM linkstatus');
			if ($linkcount > 0) {
				echo '<br>'.PHP_EOL;
				echo $table.'<br>'.PHP_EOL;
				echo '<tr>'.$td.'Reflector</td>'.$td.'IP Address</td>'.$td.'Port</td>'.$td.'Linked Time</td></tr>'.PHP_EOL;
				$results = $db->query('SELECT * FROM linkstatus');
				while ($row = $results->fetchArray(SQLITE3_ASSOC)) {
					echo '<tr>'.$td.$row[0].'<td>'.$td.$row[1].'</td>'.$td.$row[2].'</td><td style="text-align:right">'.SecToString(intval($row[3])).'</td></tr>'.PHP_EOL;
				}
				echo '<tr>'.$td.'', strtoupper($mod), '</td>'.$td.'',$cfg[$module], '</td>'.$td.'', $freq, '</td>'.$td.'', $linkstatus, '</td><td style="text-align:right">', $ctime, '</td>'.$td.'', $address, '</td></tr>'.PHP_EOL;

			} else {
				echo ' Unlinked<br>'.PHP_EOL;
			}
			$db->close();
			break;
		case 'MS':
			$rx = $cfg['Modem']['RXFrequency'] / 1000000;
			if (array_key_exists('TXFrequency', $ini['Modem']))
				$tx = $cfg['Modem']['TXFrequency'] / 1000000;
			else
				$tx = $rx;
			$afc = $cfg['Modem']['AFC'] ? "Enabled" : "Disabled";
			echo 'CC1200 Configuration:<br>'.PHP_EOL;
			echo $table.'<br>'.PHP_EOL;
			echo '<tr>'.$td.'Tx (MHz)</td>'.$td.'Rx (MHz)</td>'.$td.'AFC</td>'.$td.'Freq. Corr. (Hz)</td>'.$td.'Tx Pwr</td></tr>'.PHP_EOL;
			echo '<tr>'.$td.$tx.'</td>';
			echo $td.$rx;
			echo $td.$afc.'</td>';
			echo $td.$cfg['Modem']['TXPower'].'</td></tr></table>'.PHP_EOL;
			break;
		// case 'UR':
		// 	echo 'Send URCall:<br>'.PHP_EOL;
		// 	echo '<form method="post">'.PHP_EOL;
		// 	$mods = array();
		// 	foreach (array('a', 'b', 'c') as $mod) {
		// 		$module = 'module_'.$mod;
		// 		if (array_key_exists($module, $cfg)) {
		// 			$mods[] = strtoupper($mod);
		// 		}
		// 	}
		// 	if (count($mods) > 1) {
		// 		echo 'Module: '.PHP_EOL;
		// 		foreach ($mods as $mod) {
		// 			echo '<input type="radio" name="fmodule"', (isset($fmodule) && $fmodule==$mod) ? '"checked"' : '', ' value="$mod">', $mod, '<br>'.PHP_EOL;
		// 		}
		// 	} else
		// 		$fmodule = $mods[0];
		// 	echo 'URCall: <input type="text" name="furcall" value="', $furcall, '">'.PHP_EOL;
		// 	echo '<input type="submit" name="sendurcall" value="Send URCall"><br>'.PHP_EOL;
		// 	echo '</form>'.PHP_EOL;
		// 	if (isset($_POST['sendurcall'])) {
		// 		$furcall = $_POST['furcall'];
		// 		if (empty($_POST['fmodule'])) {
		// 			if (1==count($mods)) {
		// 				$fmodule = $mods[0];
		// 			}
		// 		} else {
		// 			$fmodule = $_POST['fmodule'];
		// 		}
		// 	}
		// 	$furcall = str_replace(' ', '_', trim(preg_replace('/[^0-9a-z_ ]/', '', strtolower($furcall))));

		// 	if (strlen($furcall)>0 && strlen($fmodule)>0) {
		// 		$command = 'qnremote '.strtolower($fmodule).' '.strtolower($cfg['ircddb_login']).' '.$furcall;
		// 		echo $command, "<br>\n";
		// 		$unused = `$command`;
		// 	}
		// 	break;
		default:
			echo 'Section "', $section, '" was not found!<br>'.PHP_EOL;
			break;
	}
}
?>
<br>
<p><i>mspot</i> Dashboard Version 02/21/26 Copyright &copy; by Thomas A. Early, N7TAE.</p>
</body>
</html>
