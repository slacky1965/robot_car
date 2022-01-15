
var speed_car = 0;
var speed_script = speed_car;
var speedup = true;
var slowdown = false;
var accelerator = false;
var forward = false;
var back = false;
var stop = true;
var turn = 90;
var command_stop = false;
var auto = false;
var driver_not_found = true;

async function command_car(command, val) {
    var str = {
        execute: command,
        value:   val
    };
    
    var id_speed = document.getElementById("speed");
    
    if (command == "forward_start") {
        command_stop = false;
        id_speed.disabled = false;
        if (stop) {
            accelerator = true;
            print_speed_script(speedup);
        } else if (back == false) {
            if (turn == 90) {
                accelerator = true;
                print_speed_script(speedup);
            }
        } else {
            accelerator = true;
            print_speed_script(slowdown);
        }        
    } else if (command == "back_start") {
        command_stop = false;
        id_speed.disabled = false;
        if (stop) {
            accelerator = true;
            print_speed_script(speedup);
        } else if (forward == false) {
            if (turn == 90) {
                accelerator = true;
                print_speed_script(speedup);
            }
        } else {
            accelerator = true;
            print_speed_script(slowdown);
        }
    } else if (command == "left_start") {
        command_stop = false;
    } else if (command == "right_start") {
        command_stop = false;
    } else if (command == "forward_stop") {
        accelerator = false;
    } else if (command == "back_stop") {
        accelerator = false;
    } else if (command == "stop") {
        accelerator = false;
    } else if (command == "auto") {
        str.value = !auto;
    }
    
    
    try {
        var response = await fetch("car", {
            method: "POST",
            headers: {
                "Content-Type": "application/json; charset=utf-8"
            },
            body: JSON.stringify(str)
        });

        if (response.ok) {
            let data = await response.json();
            let command = data.command;
            
            if (command_stop) {
                if (command == "forward_start") {
                    command_car("forward_stop");
                } else if (command == "back_start") {
                    command_car("back_stop");
                }  else if (command == "left_start") {
                    command_car("left_stop");
                } else if (command == "right_start") {
                	command_car("right_stop");
                }
            } 
            
            if (command == "forward_stop" || command == "back_stop" || command == "stop" || command == "speed" || command == "left_stop" || command == "right_stop") {
                command_stop = true;
                get_status();
            } else if (command == "auto") {
                get_status();
            }
        } else {
            var error = await response.text();
            var message = `${error}. HTTP error ${response.status}.`;
            alert(message);
        }
    }
    catch(error) {
        alert(`Error! ${error}`);
    }

}

async function get_status() {
    
    var id_index = document.getElementById("index");
    
    if (id_index) {
        try {
            var response = await fetch("car_status");
            if (response.ok) {
                var data = await response.json();
                console.log(data);
                forward = data.forward;
                back = data.back;
                turn = data.turn;
                stop = data.stop;
                auto = data.auto;
                driver_not_found = false;
                speed_car = data.speed;
                speed_script = speed_car;
                set_auto();
            } else {
                var error = await response.text();
                var message = `${error}. HTTP error ${response.status}.`;
                alert(message);
                driver_not_found = true;
                speed_car = 0;
                speed_script = speed_car;
                set_auto(error);
            }
        }
        catch (error) {
            alert(`Error! ${error}`);
            driver_not_found = true;
            speed_car = 0;
            speed_script = speed_car;
            set_auto(error);
        }
    }
}

function set_auto(message) {
    
    var id_auto    = document.getElementById("auto");
    var id_speed   = document.getElementById("speed");
    var id_forward = document.getElementById("btn_forward");
    var id_left    = document.getElementById("btn_left");
    var id_stop    = document.getElementById("btn_stop");
    var id_right   = document.getElementById("btn_right");
    var id_back    = document.getElementById("btn_back");
    var id_upload  = document.getElementById("btn_upload");
    
    print_speed(speed_car);

    if (driver_not_found) {
        id_auto.innerHTML = message;
        id_auto.disabled = true;
        id_speed.disabled = true;
        id_forward.disabled = true;
        id_left.disabled = true;
        id_stop.disabled = true;
        id_right.disabled = true;
        id_back.disabled = true;
    } else {
        if (auto) {
            id_auto.innerHTML = "Auto On";
            id_speed.disabled = true;
            id_forward.disabled = true;
            id_left.disabled = true;
            id_stop.disabled = true;
            id_right.disabled = true;
            id_back.disabled = true;
            id_upload.disabled = true;
        } else {
            id_auto.innerHTML = "Auto Off";
            id_speed.disabled = false;
            id_forward.disabled = false;
            id_left.disabled = false;
            id_stop.disabled = false;
            id_right.disabled = false;
            id_back.disabled = false;
        }
        if (stop) {
            id_speed.disabled = true;
            id_left.disabled = true;
            id_right.disabled = true;
            id_stop.disabled = true;
            id_upload.disabled = false;
        } else {
            id_upload.disabled = true;
        }
    }
}

function print_speed_script(speeding) {
    
    if (accelerator) {
        if (speeding) {
            speed_script += 2;
            if (speed_script > 255) {
                speed_script = 255;
            }
        } else {
            speed_script -= 2;
            if (speed_script < 1) {
                speed_script = 1;
            }
        }
        print_speed(speed_script);
    } else {
        return;
    }
    
    setTimeout(print_speed_script, 5, speeding);
}

function print_speed (speed) {
    
    var value_speed = document.getElementById("val_speed");
    var slider = document.getElementById("speed");
    
    slider.value = speed;
    value_speed.innerHTML = speed;
}

get_status();

// Upload zone

function setFName(elem) {
    var fileName = elem.files[0].name;
    if (elem.id == "newhtmlfile") {
        document.getElementById("uploadhtml").value = fileName;
    } else if (elem.id == "newbinfile") {
        document.getElementById("uploadbin").value = fileName;
    }
}

async function upload(elem) {
    var fileName = elem.value;
    var upload_path;
    var fileInput;
    var element;
    if (elem.id == "uploadhtml") {
        upload_path = "/upload/html/" + fileName;
        element = document.getElementById("newhtmlfile");
    } else if (elem.id == "uploadbin") {
        upload_path = "/upload/image/" + fileName;
        element = document.getElementById("newbinfile");
    }

    fileInput = element.files;

    if (fileInput.length == 0) {
        alert("No file selected!");
    } else {
        elem.disabled = true;
        element.disabled = true;

        var file = fileInput[0];

        document.getElementById("uploading").innerHTML = "Uploading! Please wait.";
        
        try {
            var response = await fetch(upload_path, {
                method: 'POST',
                body: file
            });
            if (response.ok) {
                var data = await response.text();
                alert(data);
            } else {
                var error = await response.text();
                var message = `${error}. HTTP error ${response.status}.`;
                alert(message);
            }
            
        }
        catch(error) {
            alert(`Error! ${error}`);
        }
        
        element.value = "";
        location.reload();
    }
}



