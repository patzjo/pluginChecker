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

	static int calculateValue(std::string version) {
		int result = 0;

		// (\d+)\.(\d+)\.(\d+)\+?(\d+)?
		std::regex reg("(\\d+)\\.(\\d+)\\.(\\d+)\\+?(\\d+)?");
		std::smatch match;
		
		if (std::regex_search(version, match, reg)) {

			if (match[1].matched)
				result += std::stoi(match[1]) * 10000;
			if (match[2].matched)
				result += std::stoi(match[2]) * 1000;
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

	std::regex reg("<h2 class=\"title\">([^ ]*) (.*)<\\/h2>");
	std::smatch match;

	std::regex_search(html, match, reg);
	return PluginResult(match[1], match[2]);
}

 std::vector<VersionChangeHistory> getVersionChanges(std::string pluginName, std::string oldVersion) {
	auto url = "https://pub.dev/packages/" + pluginName + "#-changelog-tab-";
	auto html = getResponse(url).str();
	std::vector <VersionChangeHistory> result;

	int pluginOldVersion = PluginResult::calculateValue(oldVersion);

	std::regex reg1("\\<h\\d class=\"hash-header\" id=\"\\d+\">(\\S*).*?(?=\\<ul\\>)(.*?(?=\\<\\/ul\\>))"), 
				reg2("\\<li\\>(.*?)\\<\\/li\\>", std::regex::flag_type::icase);
	std::smatch match1, match2;

	while (std::regex_search(html, match1, reg1)) {
		if (PluginResult::calculateValue(match1[1]) <= pluginOldVersion) {
			break;
		}
		
		std::string search2 = match1[2].str();
		std::vector<std::string> changeLog;
		VersionChangeHistory vChange;
		vChange.version = match1[1];
		while (std::regex_search(search2, match2, reg2)) {
			vChange.changeLine.push_back(match2[1]);
			result.push_back(vChange);
			search2 = match2.suffix();
		}
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
		std::cout << "Can't find pubspec.yaml on this directory!" << std::endl;
	}

	return result;
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
	std::vector<PluginResult> localPlugins = getPackagesFromPubspec("pubspec.yaml");

	bool askUpdateFlag = false;

	std::cout << "Checking plugins..." << std::endl;
	for (auto& localPlugin : localPlugins) {
		ss << "  " << localPlugin.name << " [ " << localPlugin.version << " ] on NET ";
		PluginResult remotePlugin = getPluginData(localPlugin.name);
		
		ss << "[ " << remotePlugin.version << " ] ";
		
		if (localPlugin < remotePlugin) {
			askUpdateFlag = true;
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

	if (askUpdateFlag) {
		if (askUpdate()) {
			updatePackages(localPlugins);
		}

	}

	return 0;
}