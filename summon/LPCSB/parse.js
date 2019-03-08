/* Parse LPCSB advertisements */

var parse_advertisement = function (advertisement, cb) {

    if (advertisement.localName === 'LPCSB') {
        if (advertisement.manufacturerData) {
            // Need at least 3 bytes: Two for manufacturer ID and one for service ID.
            if (advertisement.manufacturerData.length >= 3) {
                // Check that manufacturer ID and service byte are correct
                var manufacturer_id = advertisement.manufacturerData.readUIntLE(0, 2);
                var service_id = advertisement.manufacturerData.readUInt8(2);
                if (manufacturer_id == 0x02E0 && service_id == 0x31) {
                    // OK! This looks like an LPCSB packet
                    // Length should be at least header + version + 1 character
                    if (advertisement.manufacturerData.length >= (3+1+1)) {
                        var LPCSB = advertisement.manufacturerData.slice(3);

                        //Sensor ID is first: ID = 0x44, shows that the sensor is connected properly.
                        var sensorID = LPCSB.readUInt8(0);

                        //Want Little Endian (I think, need to double check this)
                        var clear=LPCSB.readUintLE(1,2);    //Clear Data
			            var red=LPCSB.readUIntLE(3,2);      //Red Data
			            var green=LPCSB.readUIntLE(5,2);    //Green Data
                        var blue=LPCSB.readUIntLE(7,2);     //Blue Data
                        var colorTemp=LPCSB.readUIntLE(9,2);//Color Temp Data
                        var lux=LPCSB.readUIntLE(11,2);     //Lux Data
                        var out = {
                            device: 'LPCSB',
                            sensorID: sensorID,
			                clear: clear,
                            red: red,
                            green: green,
                            blue: blue,
                            colorTemp: colorTemp,
                            lux: lux
			                // _meta: {
                            //     room: room
                            // },
                        };

                        cb(out);
                        return;
                    }
                }
            }
        }
    }

    cb(null);
}

//function hex2a(hexx) {
   // var hex = hexx.toString(); 
    //var string = '';
    //for (var i = 0; (i < hex.length && hex.substr(i, 2) !== '00'); i += 2)
        //string += String.fromCharCode(parseInt(hex.substr(i, 2), 16));
   // return string;
//}

module.exports = {
    parseAdvertisement: parse_advertisement
};