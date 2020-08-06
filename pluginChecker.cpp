#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <regex>
#include <vector>
#include <fstream>
#include <filesystem>

#include "curl_easy.h"
#include "curl_form.h"
#include "curl_ios.h"
#include "curl_exception.h"

namespace fs = std::filesystem;

struct VersionChangeHistory {
	std::string version;
	std::vector<std::string> changeLine;

	VersionChangeHistory(std::string version, std::vector<std::string> changeLine) {
		this->version = version;
		this->changeLine = changeLine;
	}
	VersionChangeHistory() {}
};

struct PluginResult {
	std::string name;
	std::string version;
	std::string newVersion;

	int value = 0;
	
	PluginResult(std::string name, std::string version) {
		this->name = name;
		this->version = version;
		value = calculateValue(version);
	}

	static int calculateValue(const std::string& version) {
		int result = 0;

		std::regex reg("(\\d+)\\.(\\d+)\\.(\\d+)\\+?(\\d+)?.*");
		std::smatch match;
		
		if (std::regex_search(version, match, reg)) {

			if (match[1].matched)
				result += std::stoi(match[1]) * 1000000;
			if (match[2].matched)
				result += std::stoi(match[2]) * 10000;
			if (match[3].matched)
				result += std::stoi(match[3]) * 100;
			if (match[4].matched)
				result += std::stoi(match[4]);
		}
		return result;
	}

	bool operator <  (PluginResult& other) { return value <  other.value; }
	bool operator >  (PluginResult& other) { return value >  other.value; }
	bool operator <= (PluginResult& other) { return value <= other.value; }
	bool operator >= (PluginResult& other) { return value >= other.value; }
	bool operator == (PluginResult& other) { return value == other.value; }
};

std::string stripVersionString(const std::string& version) {
	std::string result = "";

	std::regex reg("(\\d+)\\.(\\d+)\\.(\\d+)\\+?(\\d+)?.*");
	std::smatch match;

	if (std::regex_search(version, match, reg)) {
		if (match[1].matched) {
			result += match[1];
		}
		if (match[2].matched) {
			result += ".";
			result += match[2];
		}
		if (match[3].matched) {
			result += ".";
			result += match[3];
		}
		if (match[4].matched) {
			result += "+";
			result += match[4];
		}
	}
	return result;
}

std::stringstream getResponse(std::string_view url)
{
	std::stringstream str;
	curl::curl_ios<std::stringstream> writer(str);

	curl::curl_easy easy(writer);

	easy.add<CURLOPT_URL>(url.data());
	easy.add<CURLOPT_FOLLOWLOCATION>(1L);
	try
	{
		easy.perform();
	}
	catch (curl::curl_easy_exception error)
	{
		auto errors = error.get_traceback();
		error.print_traceback();
	}

	return str;
}

PluginResult getPluginData(std::string pluginName) {
	auto url = "https://pub.dev/packages/" + pluginName;
	auto html = getResponse(url).str();

	std::regex reg("<h1 class=\"title\">([^ ]*) (.*)<\\/h1>");
	std::smatch match;

	std::regex_search(html, match, reg);
	return PluginResult(match[1], match[2]);
}

 std::vector<VersionChangeHistory> getVersionChanges(std::string pluginName, std::string oldVersion) {
	auto url = "https://pub.dev/packages/" + pluginName + "/changelog";
//	auto url = "https://pub.dev/packages/" + pluginName + "#-changelog-tab-";
	auto html = getResponse(url).str();
	std::vector <VersionChangeHistory> result;

	int pluginOldVersion = PluginResult::calculateValue(oldVersion);
	// hash-header.*?id="[\d-\.]*">\D*(.*?(?=\s)) [\d\D]*?ul>([\d\D]*?<\/ul>)
	// hash-header.*?id=\"[\\d-\\.]*\">\\D*(.*?(?=\\s)) [\\d\\D]*?ul>([\\d\\D]*?<\\/ul>)
/* Old working version 
*/
	std::regex reg1("hash-header.*?id=\"[\\d-\\.]*\">\\D*(.*?(?=\\s)) [\\d\\D]*?ul>([\\d\\D]*?<\\/ul>)"),
				reg2("<li>([\\d\\D]*?)<\\/li>", std::regex::flag_type::icase);
				//	reg2("\\<li\>\\s*([\\d\\D]*?(?=\\<\\/li\\>))", std::regex::flag_type::icase);
/*
	std::regex reg1(R"(changelog-version hash-header.*?>(\S*)(.|\s)*?(?=<ul>)((.|\s)*?(?=<\/ul>)<\/ul>))", std::regex::flag_type::ECMAScript),
		reg2(R"(<li>([\\d\\D]*?)<\\/li>)", std::regex::flag_type::icase);
*/
	// changelog-version hash-header.*?>(\S*).*?(?=<ul>)(.*?(?=<\/ul>)<\/ul>)

	std::smatch match1, match2;

	while (std::regex_search(html, match1, reg1)) {
		if (PluginResult::calculateValue(match1[1]) <= pluginOldVersion) {
			break;
		}

		std::string search2 = match1[2].str();
		std::vector<std::string> changeLog;
		VersionChangeHistory vChange;
		vChange.version = stripVersionString(match1[1]);

		while (std::regex_search(search2, match2, reg2)) {
			vChange.changeLine.push_back(match2[1]);
			search2 = match2.suffix();
		}
		result.push_back(vChange);

		html = match1.suffix();
	}
	return result;
}

std::vector<PluginResult> getPackagesFromPubspec(std::string filename) {
	std::fstream file;
	std::vector <PluginResult> result;

	std::regex reg("\\s{2}([^\\W\\r\\n]+)[^\\^\\n\\r]+\\^((\\d+)\\.(\\d+)\\.(\\d+)\\+?(\\d+)?)");
	std::smatch match;

	file.open(filename, std::ios::in);
	bool pluginSection = false;
	std::string buffer;
	
	if (file.is_open()) {
		while (std::getline(file, buffer)) {

			size_t commentPosition = buffer.find_first_of('#');
			if (commentPosition != std::string::npos) {
				buffer = buffer.substr(0, commentPosition); // Remove comment

			} else if (std::regex_match(buffer, match, reg)) {
				result.push_back(PluginResult(match[1], match[2]));
			}
		}
		file.close();
	}
	else {
		std::cout << "Can't find " << filename << " on this directory!" << std::endl;
	}

	return result;
}

void checkPackagesFromPublock(std::string filename, std::vector<PluginResult>& minedPlugins) {
	std::fstream file;

	file.open(filename, std::ios::in);
	std::string fileBuffer;
	std::string fileData;

	if (file.is_open()) {
		while (std::getline(file, fileBuffer)) {
			size_t commentPosition = fileBuffer.find_first_of('#');
			if (commentPosition != std::string::npos) {
				fileBuffer = fileBuffer.substr(0, commentPosition); // Remove comment

			}
			fileData += fileBuffer;
		}
		std::regex reg("  (.*?(?=:)).*?(?=version)version: \"([\\d.]*)\"");
		std::smatch match;

		while ( std::regex_search(fileData, match, reg) ) {
			for (auto& i : minedPlugins) {
				if (i.name == match[1]) {
					bool lockHaveNewerVersion = PluginResult::calculateValue(i.version) < PluginResult::calculateValue(match[2]);
//					std::cout << "PUBSPEC: " << i.name << " " << i.version;
					if (lockHaveNewerVersion) {
//						std::cout << " LOCK file have newer version: " << match[2] << std::endl;
						i.version = match[2];
					}
/*
					else {
						std::cout << std::endl;
					}
*/
					break;
				}
			}
			fileData = match.suffix();
//			std::cout << ">" << fileData << "<" << std::endl;
		}


		file.close();
	}
	else {
		std::cout << "Can't find " << filename << " on this directory!" << std::endl;
	}
}


bool askUpdate() {
	bool result = false;
	bool validInput = false;
	char key = 0;
	while (!validInput) {
		std::cout << std::endl << "Do you want to update pubspec.yaml? y/n: ";
		key = std::cin.get();
		std::cin.clear();
		if (key == 'n' || key == 'N') { // Nope
			validInput = true;
		}
		else if (key == 'y' || key == 'Y') { // Yes
			result = true;
			validInput = true;
		}
	}

	return result;
}

std::string getNewVersionValue(std::vector<PluginResult>& plugins, std::string pluginName, std::string version) {
	std::string result("  " + pluginName + ": ^");
	for (auto& i : plugins) {
		if (i.name == pluginName) {
			if (!i.newVersion.empty()) {
				result += i.newVersion;
				std::cout << "\tUpdating " << pluginName << ": ^" << version << " to " << "^" << i.newVersion << std::endl;
				return result;
			}
		}
	}
	result += version;
	return result;
}

void updatePackages(std::vector<PluginResult>& plugins) {
	std::fstream fileOut, fileIn;
	std::string buffer;

	std::regex reg("\\s{2}([^\\W\\r\\n]+)[^\\^\\n\\r]+\\^((\\d+)\\.(\\d+)\\.(\\d+)\\+?(\\d+)?)");
	std::smatch match;

	std::cout << "\r\nStarting update..." << std::endl;
	
	fs::copy("pubspec.yaml", "pubspec.backup", fs::copy_options::overwrite_existing);

	fileIn.open("pubspec.backup", std::ios::in);
	if (fileIn.is_open()) {
		fileOut.open("pubspec.yaml", std::ios::trunc | std::ios::out);
		if (fileOut.is_open()) {
			while (getline(fileIn, buffer)) {
				if (std::regex_match(buffer, match, reg)) {
					buffer = getNewVersionValue(plugins, match[1], match[2]);
				}

				fileOut << buffer << std::endl;
			}
			fileOut.close();
		}
		fileIn.close();
	}
}


int main(int argc, char **argv)
{
	std::stringstream ss;
	std::vector<PluginResult> localPlugins;
		

	std::cout << "Loading pubspec.yaml" << std::endl;
	localPlugins = getPackagesFromPubspec("pubspec.yaml");

	if (fs::exists("pubspec.lock")) {
		std::cout << "Loading pubspec.lock" << std::endl;
		checkPackagesFromPublock("pubspec.lock", localPlugins);
	}

	std::cout << "Checking plugins..." << std::endl;
	for (auto& localPlugin : localPlugins) {
		ss << "  " << localPlugin.name << " [ " << localPlugin.version << " ] on NET ";
		PluginResult remotePlugin = getPluginData(localPlugin.name);
		
		ss << "[ " << remotePlugin.version << " ] ";
		
		if (localPlugin < remotePlugin) {
			localPlugin.newVersion = remotePlugin.version;
		}
		std::cout << std::left << std::setw(80) << ss.str() << ((localPlugin < remotePlugin) ? "Update!" : "Got newest version") << std::endl;
		ss.str("");

		if (!localPlugin.newVersion.empty()) {
			std::vector<VersionChangeHistory> result = getVersionChanges(localPlugin.name, localPlugin.version);
			for (auto& i : result) {
				std::cout << "\t" << i.version << std::endl;
				for (auto& line : i.changeLine) {
					std::cout << "\t * " << line << std::endl;
				}
			}
		}

	}
	return 0;
}