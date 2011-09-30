use Test::More;

plan tests => 3;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;

my $polyline = Slic3r::Polyline::Closed->cast([
    [5,0], [10,0], [15,0], [20,0], [20,10], [20,30], [0,0],
]);

$polyline->merge_continuous_lines;
is scalar(@{$polyline->points}), 3, 'merge_continuous_lines';

my $gear = [
    [144.9694,317.1543], [145.4181,301.5633], [146.3466,296.921], [131.8436,294.1643], [131.7467,294.1464], 
    [121.7238,291.5082], [117.1631,290.2776], [107.9198,308.2068], [100.1735,304.5101], [104.9896,290.3672], 
    [106.6511,286.2133], [93.453,279.2327], [81.0065,271.4171], [67.7886,286.5055], [60.7927,280.1127], 
    [69.3928,268.2566], [72.7271,264.9224], [61.8152,253.9959], [52.2273,242.8494], [47.5799,245.7224], 
    [34.6577,252.6559], [30.3369,245.2236], [42.1712,236.3251], [46.1122,233.9605], [43.2099,228.4876], 
    [35.0862,211.5672], [33.1441,207.0856], [13.3923,212.1895], [10.6572,203.3273], [6.0707,204.8561], 
    [7.2775,204.4259], [29.6713,196.3631], [25.9815,172.1277], [25.4589,167.2745], [19.8337,167.0129], 
    [5.0625,166.3346], [5.0625,156.9425], [5.3701,156.9282], [21.8636,156.1628], [25.3713,156.4613], 
    [25.4243,155.9976], [29.3432,155.8157], [30.3838,149.3549], [26.3596,147.8137], [27.1085,141.2604], 
    [29.8466,126.8337], [24.5841,124.9201], [10.6664,119.8989], [13.4454,110.9264], [33.1886,116.0691], 
    [38.817,103.1819], [45.8311,89.8133], [30.4286,76.81], [35.7686,70.0812], [48.0879,77.6873], 
    [51.564,81.1635], [61.9006,69.1791], [72.3019,58.7916], [60.5509,42.5416], [68.3369,37.1532], 
    [77.9524,48.1338], [80.405,52.2215], [92.5632,44.5992], [93.0123,44.3223], [106.3561,37.2056], 
    [100.8631,17.4679], [108.759,14.3778], [107.3148,11.1283], [117.0002,32.8627], [140.9109,27.3974], 
    [145.7004,26.4994], [145.1346,6.1011], [154.502,5.4063], [156.9398,25.6501], [171.0557,26.2017], 
    [181.3139,27.323], [186.2377,27.8532], [191.6031,8.5474], [200.6724,11.2756], [197.2362,30.2334], 
    [220.0789,39.1906], [224.3261,41.031], [236.3506,24.4291], [243.6897,28.6723], [234.2956,46.7747], 
    [245.6562,55.1643], [257.2523,65.0901], [261.4374,61.5679], [273.1709,52.8031], [278.555,59.5164], 
    [268.4334,69.8001], [264.1615,72.3633], [268.2763,77.9442], [278.8488,93.5305], [281.4596,97.6332], 
    [286.4487,95.5191], [300.2821,90.5903], [303.4456,98.5849], [286.4523,107.7253], [293.7063,131.1779], 
    [294.9748,135.8787], [314.918,133.8172], [315.6941,143.2589], [300.9234,146.1746], [296.6419,147.0309], 
    [297.1839,161.7052], [296.6136,176.3942], [302.1147,177.4857], [316.603,180.3608], [317.1658,176.7341], 
    [315.215,189.6589], [315.1749,189.6548], [294.9411,187.5222], [291.13,201.7233], [286.2615,215.5916], 
    [291.1944,218.2545], [303.9158,225.1271], [299.2384,233.3694], [285.7165,227.6001], [281.7091,225.1956], 
    [273.8981,237.6457], [268.3486,245.2248], [267.4538,246.4414], [264.8496,250.0221], [268.6392,253.896], 
    [278.5017,265.2131], [272.721,271.4403], [257.2776,258.3579], [234.4345,276.5687], [242.6222,294.8315], 
    [234.9061,298.5798], [227.0321,286.2841], [225.2505,281.8301], [211.5387,287.8187], [202.3025,291.0935], 
    [197.307,292.831], [199.808,313.1906], [191.5298,315.0787], [187.3082,299.8172], [186.4201,295.3766], 
    [180.595,296.0487], [161.7854,297.4248], [156.8058,297.6214], [154.3395,317.8592],
];
$polyline = Slic3r::Polyline::Closed->cast($gear);
$polyline->merge_continuous_lines;
diag sprintf "original points: %d\nnew points: %d", scalar(@$gear), scalar(@{$polyline->points});
ok (@{$polyline->points} < @$gear), 'gear was simplified using merge_continuous_lines';

my $num_points = scalar @{$polyline->points};
$polyline->cleanup;
diag sprintf "original points: %d\nnew points: %d", $num_points, scalar(@{$polyline->points});
ok (@{$polyline->points} < $num_points), 'gear was further simplified using Douglas-Peucker';
