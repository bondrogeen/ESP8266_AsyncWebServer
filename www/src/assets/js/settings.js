var settings;
function send(page, data, callback, type) {
  var req = new XMLHttpRequest();
  req.open(type || "POST", page, true);
  req.setRequestHeader('Content-Type', 'application/json; charset=utf-8');
  req.addEventListener("load", function () {
    if (req.status === 200) {
      try {
        var data = JSON.parse(req.response)
        callback(data)
      } catch (err) {
        callback(false)
      }
    } else {
      callback(false)
    }
  });
  req.send(JSON.stringify(data));
}

function $(val) {
  return document.getElementById(val);
}

function reboot() {
  send("reboot", {}, function (res) {

  });
  $('modal').classList.add("hide");
}

function loadSettings() {
  send("settings", {}, function (res) {
    $('loader').classList.add('hide');
    if (res) {
      for (key in res) {
        $(key).value = res[key];
      }
      settings = res;
    }
  });
}

function logout() {
  document.cookie = "id=";
  window.location = window.location.href.replace(/:\/\//, '://log:out@');
  location.href = '/';
}

function save() {
  $('modal').classList.add("hide");
  var data = {};
  var values = document.getElementsByClassName('value');
  for (var i = 0, len = values.length | 0; i < len; i = i + 1 | 0) {
    var id = values[i].id;
    var value;
    if (values[i].tagName == "SELECT") {
      for (var s = 0; s < values[i].options.length | 0; s++) {
        if (values[i].options[s].selected) {
          value = values[i].options[s].value
          if (value !== '') data[id] = (/^-{0,1}\d+$/.test(value)) ? Number(value) : value
        }
      }
    }
    if (values[i].tagName == "INPUT") {
      value = values[i].value
      console.log()
      if (!values[i].disabled && value !== '') {
        data[id] = (/^-{0,1}\d+$/.test(value)) ? Number(value) : value
      } else {
        if (!values[i].disabled) {
          values[i].focus();
          return;
        }
      }
    }
  }

  console.log(data)
  $('modal').classList.remove("hide");
  // arr.forEach(function (item, i, arr) {
  //   if (item === "mode") {
  //     data[item] = +check_sel(item);
  //   } else if (item === "auth") {
  //     data[item] = check_sel(item) == "true";
  //   } else {
  //     var x = $(item).value;
  //     if (x || x !== '') data[item] = x;
  //   }
  // });
  // if (+check_sel("mode") === 0) {
  //   if (!confirm("Attention !!! Wi-Fi will be disabled, do you really want it?")) return;
  // }
  // $('loader').classList.remove('hide')
  // $('modal').classList.add("hide");
  send("settings", data, function (res) {
    console.log(res)
  }, "PUT");
}

function scan() {
  $('loader').classList.remove('hide')
  send("scan", {}, function (res) {
    if (res && res.length > 0) {
      var buf = '';
      res.forEach(function (item, i, res) {
        buf += '<li id="' + item.ssid + '"><b>' + item.ssid + '</b> rssi : ' + item.rssi + ' channel : ' + item.channel + '</li>';
      });
      $('list').innerHTML = buf;
    } else {
      $('list').innerHTML = '<li>No networks found. Try scanning again</li>';
    }
    $('list').style.display = 'block';
    $('loader').classList.add('hide')
  });
}

window.onload = function () {
  loadSettings();
  document.body.addEventListener("click", function (event) {
    var id = event.target.id;
    console.log(id)
    if (id === "search") scan();
    if (id === "btn_exit") logout();
    if (id === "save_m"); reboot();
    if (id === "btn_save") save();
    if (id === "close" || id === "close_m") $('modal').classList.add("hide");
    if (event.target.tagName === "LI") {
      var a = $('list');
      if (id) {
        $('wifi_ssid').value = id;
        $('wifi_pass').value = "";
        $('wifi_pass').focus();
        // $('mode').value = "1";
      }
      a.style.display = 'none';
    }
  });
};
