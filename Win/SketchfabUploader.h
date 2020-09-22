#pragma once

#include <curl/include/curl.h>
#include <picojson/picojson.h>
#include "stdafx.h"
#include "Resource.h"

#define SKETCHFAB_SERVER "https://sketchfab.com"
#define SKETCHFAB_MODELS SKETCHFAB_SERVER "/models"
#define SKETCHFAB_MODELS_API SKETCHFAB_SERVER "/v2/models"

typedef std::map<std::string, std::string> attributes;

struct ProgressbarUpdater {
    CURL *curl;
    HWND progressBar;
    bool cancel;
};

// Taken from https://curl.haxx.se/libcurl/c/progressfunc.html
int progress_callback(void *clientp,
    curl_off_t dltotal, curl_off_t dlnow,
    curl_off_t ultotal, curl_off_t ulnow)
{

    UNREFERENCED_PARAMETER(dltotal);
    UNREFERENCED_PARAMETER(dlnow);
    struct ProgressbarUpdater *myp = (struct ProgressbarUpdater *)clientp;

    if(myp->cancel)
        return 1;

    double curtime = 0;
    curl_easy_getinfo(myp->curl, CURLINFO_TOTAL_TIME, &curtime);

    curl_off_t total_percen = 0;
    if(ultotal > CURL_OFF_T_C(100)) {
        total_percen = ulnow / (ultotal / CURL_OFF_T_C(100));
    }

    SendMessage(myp->progressBar, PBM_SETPOS, (WPARAM)float(total_percen), 0);

    return 0;
};

static size_t WriteMemoryCallback(char *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

class SketchfabV2Uploader {
private:
    struct ProgressbarUpdater prog;
public:
    void abort()
    {
        prog.cancel=true;
    }
    std::pair<bool, std::string> upload(HWND progressBar,
                                        const std::string& token,
                                        const std::string& filepath,
                                        const std::string& name = std::string(),
                                        const std::string& description = std::string(),
                                        const std::string& tags = std::string(),
                                        const bool draft = false,
                                        const bool setPrivate = false,
                                        const std::string& password = std::string())
    {
        //std::string modelid;
        std::map<std::string, std::string> parameters, files;
        HWND pprogressBar = GetDlgItem(progressBar, IDC_SKFB_UPLOAD_PROGRESS);

        // Settings request parameters
        parameters["token"] = token;
        parameters["isPublished"] = (draft? "0" : "1");
        parameters["private"] = (setPrivate ? "1" : "0");
        parameters["password"] = password;
        parameters["name"] = name;
        parameters["tags"] = std::string("mineways ") + std::string("minecraft ") + tags;
        parameters["description"] = description;
        parameters["source"] = "mineways";
        files["modelFile"] = filepath;

        std::pair<int, std::string> response = post(SKETCHFAB_MODELS_API, files, parameters, pprogressBar);

        // Upload v2 returns a status 201 not 200
        bool success=false;
        if (response.first == 201)
          success=true;

        std::string value((success ? model_url(get_json_key(response.second, "uid")) : get_json_key(response.second, "detail")));

        return std::pair<bool, std::string>(success, value);
    }


    std::pair<int, std::string> post(const std::string& url,
                                     const attributes& files = attributes(),
                                     const attributes& parameters = attributes(),
                                     HWND progressBar=NULL)
    {
        CURL *curl;
        CURLcode res;
        long http_code;
        std::string response;
        struct curl_httppost *formpost = NULL;
        struct curl_httppost *lastptr = NULL;
        http_code = 0;

        if (url.empty()) {
            std::cout << "Missing url parameter in post parameters" << std::endl;
            return std::pair<int, std::string>(-1, std::string());
        }
        if (files.empty()) {
            std::cout << "Missing files in post parameters" << std::endl;
            return std::pair<int, std::string>(-2, std::string());
        }

        for (attributes::const_iterator file = files.begin(); file != files.end(); ++file) {
            std::cout << file->first.c_str();
			curl_formadd(&formpost,
				         &lastptr,
				         CURLFORM_COPYNAME, file->first.c_str(),
				         CURLFORM_FILE, file->second.c_str(),
				         CURLFORM_END);
        }

        for (attributes::const_iterator parameter = parameters.begin(); parameter != parameters.end(); ++parameter) {
              if (!parameter->second.empty()) {
                curl_formadd(&formpost,
                             &lastptr,
                             CURLFORM_COPYNAME, parameter->first.c_str(),
                             CURLFORM_COPYCONTENTS, parameter->second.c_str(),
                             CURLFORM_END);
              }
        }

    // Setting curl
    curl = curl_easy_init();

    if (curl) {
        prog.curl = curl;
        prog.progressBar = progressBar;
        prog.cancel = false;

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
        curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &prog);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 900L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            // Upload has been cancelled
            if(prog.cancel)
                return std::pair<int, std::string>(1, "{\"detail\":\" Canceled by the user\" } ");
            else
                return std::pair<int, std::string>(1, "{\"detail\":\"curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)) + "\" } ");
        }
        else {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        }
        SendMessage(progressBar, PBM_SETPOS,100, 0);
        curl_easy_cleanup(curl);
        curl_formfree(formpost);
    }

    return std::pair<int, std::string>(http_code, response);
}

std::pair<int, std::string> get(const std::string& url,
                                const attributes& parameters = attributes())
{
    CURL *curl;
    CURLcode res;
    //std::string model_url;
    std::string response;
    long http_code;

    if (url.empty()) {
        std::cout << "Missing url parameters in get parameters" << std::endl;
        return std::pair<int, std::string>(-1, std::string());
    }

    std::string options;
    for (attributes::const_iterator parameter = parameters.begin(); parameter != parameters.end(); ++parameter) {
        if (options.empty()) {
            options = "?";
        }
        else {
            options += "&";
        }
        options += parameter->first + "=" + parameter->second;
    }

    http_code = 0;
    curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, (url + options).c_str());
        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform failed:" << curl_easy_strerror(res) << std::endl;
            std::cerr << "Model polling returned http code: " << http_code << std::endl;
        }
        else {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        }

        curl_easy_cleanup(curl);
    }

    return std::pair<int, std::string>(http_code, response);
}

std::string model_url(const std::string& modelid) const {
    return std::string(SKETCHFAB_MODELS) + "/" + modelid;
}

protected:
    std::string get_json_key(const std::string& json, const std::string& key) const {
        picojson::value v;
        //std::string err = 
		picojson::parse(v, json);

        if (v.is<picojson::object>()) {
            picojson::object obj = v.get<picojson::object>();
            return obj[key].to_str();
        }
        return std::string();
    }
};
