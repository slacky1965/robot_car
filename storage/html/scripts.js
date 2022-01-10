
var speed_car = 0;
var speed_script = speed_car;
var print_speedup = false;
var print_slowdown = false;
var forward = false;
var back = false;
var stop = true;
var turn = 90;
var command_stop = false;

function print_speedup_script() {

    if (print_speedup) {
        var output = document.getElementById("val_speed");
        if (output) {
            var slider = document.getElementById("speed");
            slider.value = speed_script;
            output.innerHTML = speed_script++;
            speed_script++;
            if (speed_script > 255) {
                speed_script = 255;
            }
        }
    } else {
        return;
    }
    
    setTimeout(print_speedup_script, 5);
}   

function print_slowdown_script() {

    if (print_slowdown) {
        var output = document.getElementById("val_speed");
        if (output) {
            var slider = document.getElementById("speed");
            slider.value = speed_script;
            output.innerHTML = speed_script--;
            speed_script--;
            if (speed_script < 1) {
                speed_script = 1;
            }
        }
    } else {
        return;
    }
    
    setTimeout(print_slowdown_script, 5);
}   

function print_speed (speed) {
    var output = document.getElementById("val_speed");
    if (output) {
        output.innerHTML = speed;
    }
}

async function command_car(command, val) {
    var str = {
        execute: command,
        value:   val
    };
    
    console.log("Enter command - "+command);
    
    if (command == "forward_start") {
        command_stop = false;
        if (stop) {
            stop = false;
            print_speedup = true;
            print_speedup_script();
        } else if (back == false) {
            if (turn == 90) {
                print_speedup = true;
                print_speedup_script();
            }
        } else {
            print_slowdown = true;
            print_slowdown_script()
        }        
    } else if (command == "back_start") {
        command_stop = false;
        if (stop) {
            stop = false;
            print_speedup = true;
            print_speedup_script();
        } else if (forward == false) {
            if (turn == 90) {
                print_speedup = true;
                print_speedup_script();
            }
        } else {
            print_slowdown = true;
            print_slowdown_script()
        }
    } else if (command == "left_start") {
        command_stop = false;
    } else if (command == "right_start") {
        command_stop = false;
    } else if (command == "forward_stop") {
        print_speedup = false; 
        print_slowdown = false;
    } else if (command == "back_stop") {
        print_speedup = false; 
        print_slowdown = false;
    } else if (command == "stop") {
        print_speedup = false; 
        print_slowdown = false;
        stop = true;
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
            }
            console.log("Return command from server - "+data.command);
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
    
    try {
        var response = await fetch("car_status");
        if (response.ok) {
            var data = await response.json();
            forward = data.forward;
            back = data.back;
            turn = data.turn;
            stop = data.stop;
            speed_car = data.speed;
            speed_script = speed_car;
            print_speed(speed_car);
            var slider = document.getElementById("speed");
            if (slider) {
                slider.value = speed_car;
            }
        } else {
            var error = await response.text();
            var message = `${error}. HTTP error ${response.status}.`;
            alert(message);
        }
    }
    catch (error) {
        alert(`Error! ${error}`);
    }
}


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

get_status();
//print_speedup_script();
//print_slowdown_script();


