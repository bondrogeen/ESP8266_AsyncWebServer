var settings;function send(e,s,n,t){var i=new XMLHttpRequest;i.open(t||"POST",e,!0),i.setRequestHeader("Content-Type","application/json; charset=utf-8"),i.addEventListener("load",function(){if(200===i.status)try{var e=JSON.parse(i.response);n(e)}catch(e){n(!1)}else n(!1)}),i.send(JSON.stringify(s))}function $(e){return document.getElementById(e)}function loadSettings(){send("settings",{},function(e){if($("loader").classList.add("hide"),e){for(key in e)$(key).value=e[key];settings=e}})}function logout(){document.cookie="id=",window.location=window.location.href.replace(/:\/\//,"://log:out@"),location.href="/"}function save(){$("modal").classList.add("hide");for(var e={},s=document.getElementsByClassName("value"),n=0,t=0|s.length;n<t;n=n+1|0){var i,o=s[n].id;if("SELECT"==s[n].tagName)for(var a=0;a<s[n].options.length|0;a++)s[n].options[a].selected&&""!==(i=s[n].options[a].value)&&(e[o]=/^-{0,1}\d+$/.test(i)?Number(i):i);if("INPUT"==s[n].tagName)if(i=s[n].value,console.log(),s[n].disabled||""===i){if(!s[n].disabled)return void s[n].focus()}else e[o]=/^-{0,1}\d+$/.test(i)?Number(i):i}console.log(e),$("modal").classList.remove("hide"),send("settings",e,function(e){console.log(e)},"PUT")}function scan(){$("loader").classList.remove("hide"),send("scan",{},function(e){if(e&&0<e.length){var t="";e.forEach(function(e,s,n){t+='<li id="'+e.ssid+'"><b>'+e.ssid+"</b> rssi : "+e.rssi+" channel : "+e.channel+"</li>"}),$("list").innerHTML=t}else $("list").innerHTML="<li>No networks found. Try scanning again</li>";$("list").style.display="block",$("loader").classList.add("hide")})}window.onload=function(){loadSettings(),document.body.addEventListener("click",function(e){var s=e.target.id;if(console.log(s),"search"===s&&scan(),"btn_exit"===s&&logout(),"btn_save"===s&&save(),"close"!==s&&"close_m"!==s||$("modal").classList.add("hide"),"LI"===e.target.tagName){var n=$("list");s&&($("wifi_ssid").value=s,$("wifi_pass").value="",$("wifi_pass").focus()),n.style.display="none"}})};