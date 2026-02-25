<!DOCTYPE html>
<?php
$ifn = '/home/USER/mspot/mspot.ini';
$inidata = parse_ini_file($ifn, true);
if (false === $inidata)
	die('Could not parse mspot ini file '.$ifn);

// get the sqlite3 database pathway
$dbfile = $inidata['Gateway']['DBPath'];

// here are some useful defines for html
function Table(int $b)
{
	echo '<table cellpadding="1" border="'.$b.'">'.PHP_EOL;
}

function Caption($title)
{
	echo '<caption>'.$title.'</caption>'.PHP_EOL;
}

function Td($p, $s)
{
	echo '<td style="text-align:';
	if ($p < 0) 
		echo 'left';
	elseif ($p > 0)
		echo 'right';
	else
		echo 'center';
	echo '">';
	echo $s;
	echo '</td>';
}

function Th($p, $s)
{
	echo '<th style="text-align:';
	if ($p < 0) 
		echo 'left';
	elseif ($p > 0)
		echo 'right';
	else
		echo 'center';
	echo '">';
	echo $s;
	echo '</th>';
}

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

// other helper functions
function SecToString(int $sec) {
	if ($sec >= 86400)
		return sprintf("%0.2f days", $sec/86400);
	$hrs = intdiv($sec, 3600);
	$sec %= 3600;
	$min = intdiv($sec, 60);
	$sec %= 60;
	if ($hrs) return str_replace(' ', '&nbsp;', sprintf("%2d hr  %2d min", $hrs, $min));
	if ($min) return str_replace(' ', '&nbsp;', sprintf("%2d min %2d sec", $min, $sec));
	return str_replace(' ', '&nbsp;', sprintf("%2d sec", $sec));
}

function SrcLinkToQRZ(string $src)
{
	$cs = $src;
	foreach(array(' ', '-', '/', '.') as $chr) {
		if (str_contains($cs, $chr))
			$cs = strstr($cs, $chr, true);
	}
	$tail = substr_replace($src, '', 0, strlen($cs));
	$link = '<a*target="_blank"*href="https://www.qrz.com/db/'.$cs.'">'.$cs.'</a>'.$tail;
	$len = strlen($src);
	while ($len < 10) {
		$link .= ' ';
		$len += 1;
	}
	return str_replace('*', ' ', str_replace(' ', '&nbsp;', $link));
}

//example URL: https://www.google.com/maps?q=+32.4090013,-110.9943204
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
	return str_replace('*', ' ', $str);
}

// get all the data needed for the page
$db = new SQLite3($dbfile, SQLITE3_OPEN_READONLY);
$showlist = explode(',', trim($inidata['Dashboard']['ShowOrder']));
foreach ($showlist as $section) {
	switch ($section) {
		case 'IP':
			$ipaddr['internal'] = GetIP('internal');
			$hasv6 = $inidata['Gateway']['EnableIPv6'];
			$hasv4 = $inidata['Gateway']['EnableIPv4'];
			if ($hasv4) $ipaddr['ipv4'] = GetIP('ipv4');
			if ($hasv6) $ipaddr['ipv6'] = GetIP('ipv6');
			break;
		case 'LH':
			$lheard = array();
			$ss = 'SELECT src,fromnode,dst,framecount,mode,maidenhead,latitude,longitude,strftime("%s","now")-lasttime FROM lastheard ORDER BY lasttime DESC LIMIT '.$inidata['Dashboard']['LastHeardSize'].' ';
			if ($stmnt = $db->prepare($ss)) {
				if ($result = $stmnt->execute()) {
					$i = 0;
					while ($row = $result->FetchArray(SQLITE3_NUM)) {
						$lheard[$i] = array(
							'src'        => SrcLinkToQRZ($row[0]),
							'fromnode'   => $row[1],
							'dst'        => $row[2],
							'framecount' => ($row[3] > 0) ? sprintf('%.2f sec', 0.04 * $row[3]) : '..TXing..',
							'mode'       => $row[4],
							'maidenhead' => Maidenhead($row[5], $row[6], $row[7]),
							'lasttime'   => SecToString($row[8]),
						);
						$i++;
					}
					$result->finalize();
				}
				$stmnt->close();
			}
			break;
		case 'LS':
			$linkcount = $db->querySingle('SELECT COUNT(*) FROM linkstatus');
			if ($linkcount > 0) {
				$results = $db->query('SELECT reflector,address,port,strftime("%s","now")-linked_time FROM linkstatus');
				while ($row = $results->FetchArray(SQLITE3_NUM)) {
					$lstatus = '<tr><td>'.$row[0].'</td><td>'.$row[1].'</td><td>'.$row[2].'</td><td>'.SecToString($row[3]).'</td></tr>'.PHP_EOL;
				}
			} else {
				$lstatus = '<tr><td colspan="4">unlinked</td></tr>'.PHP_EOL;
			}
			break;
		case 'SY':
			$hn = trim(`uname -n`);
			$kn = trim(`uname -rmo`);
			$ps = explode(' ', trim(`ps -eo "%p %C %z %c" | grep mspot | grep -v grep`));
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
			$temperature = str_replace("'", '&deg;', substr(`vcgencmd measure_temp`, 5));
			break;
	}
}
$db->close();
?>

<html>
<head>
<title><i>mspot</i> <?php echo $inidata['Repeater']['Callsign']; ?> Dashboard</title>
<meta http-equiv="refresh" content="<?php echo $inidata['Dashboard']['RefreshPeriod'];?>">
</head>
<style>
table {
	font-family:Arial, Helvetica, sans-serif;
	border-collapse: collapse;
}

td {
	font-family:'Courier New', Courier, monospace;
}

th {
	color: #a52a2a;
}

th, td {
    padding-top: 2px;
    padding-bottom: 1px;
    padding-right: 10px;
    padding-left: 10px;
}

tr:nth-child(even) {
	background-color: #eeeeee;
}

caption {
	font-family:'Times New Roman', Times, serif;
	font-weight: bold;
	color: #0000b0
}
</style>
<body>
<h3><?php echo $inidata['Repeater']['Callsign']; ?> <i>mspot</i> Dashboard</h2>

<?php
foreach($showlist as $section) {
	switch ($section) {
		case 'IP':
			Table(1);
			Caption('IP Addresses');
			echo '<tr>';
			Th(0, 'Internal');
			if ($hasv4) Th(0, 'IPV4');
			if ($hasv6) Th(0, 'IPV6');
			echo '</tr>'.PHP_EOL;
			echo '<tr>';
			Td(0, $ipaddr['internal']);
			if ($hasv4) Td(0, $ipaddr['ipv4']);
			if ($hasv6) Td(0, $ipaddr['ipv6']);
			echo '</tr></table><br>'.PHP_EOL;
			break;
		case 'LH':
			Table(0);
			Caption('Last Heard');
			echo '<tr>';
			Th(-1, 'User');
			Th(-1, 'From');
			Th(-1, "Destination");
			Th(0, 'TxTime');
			Th(0, 'Mode');
			Th(0, 'GNSS');
			Th(0, 'Heard');
			echo '</tr>'.PHP_EOL;
			foreach ($lheard as $lhrow) {
				echo '<tr>';
				Td(-1, $lhrow['src']);
				Td(-1, $lhrow['fromnode']);
				Td(-1, $lhrow['dst']);
				Td(0,  $lhrow['framecount']);
				Td(0,  $lhrow['mode']);
				Td(0,  $lhrow['maidenhead']);
				Td(1,  $lhrow['lasttime'].' ago');
				echo '</tr>'.PHP_EOL;
			}
			echo '</table><br>'.PHP_EOL;
			break;
		case 'LS':
			Table('1');
			Caption('Link Status');
			echo '<tr>';
			Th(0, 'Reflector');
			Th(0, 'IP Address');
			Th(0, 'Port');
			Th(0, 'Linked');
			echo '</tr>'.PHP_EOL;
			echo $lstatus;
			echo '</table><br>'.PHP_EOL;
			break;
		case 'MS':
			$rx = $inidata['Modem']['RXFrequency'] / 1000000;
			if (array_key_exists('TXFrequency', $inidata['Modem']))
				$tx = $inidata['Modem']['TXFrequency'] / 1000000;
			else
				$tx = $rx;
			$afc = $inidata['Modem']['AFC'] ? "Enabled" : "Disabled";
			Table(1);
			Caption('CC1200 Configuration');
			echo '<tr>';
			Th(0, 'Tx (MHz)');
			Th(0, 'Rx (MHz)');
			Th(0, 'AFC');
			Th(0, 'Freq. Corr. (Hz)');
			Th(0, 'Tx Pwr');
			echo '<tr>'.PHP_EOL;
			Td(0, $tx);
			Td(0, $rx);
			Td(0, $afc);
			Td(0, $inidata['Modem']['FreqCorrection']);
			Td(0, $inidata['Modem']['TXPower']);
			echo '</tr></table><br>'.PHP_EOL;
			break;
		case 'SY':
			echo Table(1);
			Caption('System Info');
			echo '<tr>';
			Th(1, 'PID');
			Td(-1, $ps[0]);
			echo '</tr>'.PHP_EOL;
			echo '<tr>';
			Th(1, '%CPU');
			Td(-1, $ps[1]);
			echo '</tr>'.PHP_EOL;
			echo '<tr>';
			Th(1, 'VSIZE');
			Td(-1, $ps[2]);
			echo '</tr>'.PHP_EOL;
			echo '<tr>';
			Th(1, 'Temp');
			Td(-1, $temperature);
			echo '</tr>'.PHP_EOL;
			echo '<tr>';
			Th(1, 'cpu');
			Td(-1, $cu);
			echo '</tr>'.PHP_EOL;
			echo '<tr>';
			Th(1, 'Kernel');
			Td(-1, $kn);
			echo '</tr>'.PHP_EOL;
			echo '<tr>';
			Th(1, 'OS');
			Td(-1, $os);
			echo '</tr>'.PHP_EOL;
			echo '<tr>';
			Th(1, 'Hostname');
			Td(-1, $hn);
			echo '</tr>'.PHP_EOL.'</table><br>'.PHP_EOL;
			break;
		default:
			echo 'Section "', $section, '" was not found!<br>'.PHP_EOL;
			break;
	}
}
?>
<p><i>mspot</i> Dashboard V# 1.0.0 Copyright &copy; 2026 by Thomas A. Early, N7TAE.</p>
</body>
</html>
