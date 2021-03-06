/*
BodySlide and Outfit Studio
Copyright (C) 2017  Caliente & ousnius
See the included LICENSE file
*/

#include "ConfigurationManager.h"

ConfigurationItem::~ConfigurationItem() {
	for (auto &it : properties)
		delete it;

	for (auto &it : children)
		delete it;

	properties.clear();
	children.clear();
}

int ConfigurationItem::SettingFromXML(XMLElement* xml) {
	name = xml->Value();
	if (parent) {
		path = parent->path + "/";
		path += name;
	}
	else {
		level = 0;
		path = name;
	}
	if (xml->GetText() != nullptr)
		value = xml->GetText();

	const XMLAttribute* attr = xml->FirstAttribute();
	while (attr) {
		ConfigurationItem* newCI = new ConfigurationItem();
		newCI->parent = this;
		newCI->level = level + 1;
		newCI->isProp = true;
		newCI->name = attr->Name();
		newCI->value = attr->Value();
		newCI->path = path + ".";
		newCI->path += newCI->name;
		properties.push_back(newCI);

		attr = attr->Next();
	}

	XMLNode* child = xml->FirstChild();
	while (child) {
		if (child->ToComment())
			children.push_back(new ConfigurationItem(child->ToComment()->Value(), this, level + 1));
		else if (child->ToElement())
			children.push_back(new ConfigurationItem(child->ToElement(), this, level + 1));
		child = child->NextSibling();
	}

	return 0;
}

void ConfigurationItem::ToXML(XMLElement* elem) {
	XMLElement* newElement = elem->GetDocument()->NewElement(name.c_str());
	XMLElement* element = elem->InsertEndChild(newElement)->ToElement();

	for (auto &prop : properties) {
		if (prop->isDefault)
			continue;
		element->SetAttribute(prop->name.c_str(), prop->value.c_str());
	}
	for (auto &child : children) {
		if (child->isDefault)
			continue;
		if (child->isComment) {
			XMLComment* newComment = elem->GetDocument()->NewComment(child->value.c_str());
			element->InsertEndChild(newComment);
		}
		else
			child->ToXML(element);
	}

	XMLText* newText = elem->GetDocument()->NewText(value.c_str());
	element->InsertEndChild(newText);
}

int ConfigurationItem::EnumerateProperties(std::vector<ConfigurationItem*>& outList) {
	int count = 0;
	for (auto &prop : properties) {
		count++;
		outList.push_back(prop);
	}
	return count;
}

int ConfigurationItem::EnumerateProperties(std::string& outList) {
	int count = 0;
	outList = "";

	for (auto &prop : properties) {
		count++;
		outList += prop->name + "=\"" + prop->value + "\" ";
	}

	return count;
}

int ConfigurationItem::EnumerateChildren(std::vector<ConfigurationItem*>& outList, bool withProperties, bool traverse) {
	int count = 0;
	for (auto &child : children) {
		if (child->isComment)
			continue;

		count++;
		outList.push_back(child);
		if (traverse) {
			if (withProperties)
				count += child->EnumerateProperties(outList);
			count += child->EnumerateChildren(outList, withProperties, traverse);
		}
	}
	return count;
}

int ConfigurationItem::EnumerateChildren(const std::string& inName, std::vector<std::string>& outList) {
	for (auto &child : children) {
		if (child->isComment)
			continue;
		if (child->Match(inName))
			outList.push_back(child->value);
	}
	return outList.size();
}

int ConfigurationItem::EnumerateChildrenProperty(const std::string& inName, const std::string& propertyName, std::vector<std::string>& outList) {
	for (auto &child : children) {
		if (child->isComment)
			continue;

		if (child->Match(inName)) {
			ConfigurationItem* prop = child->FindProperty(propertyName);
			if (prop)
				outList.push_back(prop->value);
		}
	}
	return outList.size();
}

ConfigurationItem* ConfigurationItem::FindChild(const std::string& inName, bool recurse) {
	ConfigurationItem* found = nullptr;
	size_t pos = inName.find_first_of("/.");

	if (pos != std::string::npos) {
		std::string tmpName = inName.substr(0, pos);
		for (auto &child : children) {
			if (child->isComment)
				continue;
			if (child->Match(tmpName))
				found = child;
		}

		if (!found)
			return nullptr;
		else if (!recurse)
			return found;

		if (inName.at(pos) == '/')
			found = found->FindChild(inName.substr(pos + 1));
		else if (inName.at(pos) == '.')
			found = found->FindProperty(inName.substr(pos + 1));

		if (!found)
			return nullptr;
	}
	else {
		for (auto &child : children) {
			if (child->isComment)
				continue;
			if (child->Match(inName))
				found = child;
		}
	}

	// return nullptr if nothing was found
	return found;
}

ConfigurationItem* ConfigurationItem::AddChild(const std::string& inName, const std::string& val, bool isElement) {
	ConfigurationItem* found = nullptr;
	int pos = inName.find_first_of("/.");
	std::string tmpName = inName.substr(0, pos);

	for (auto &child : children) {
		if (child->isComment)
			continue;
		if (child->Match(tmpName))
			found = child;
	}
	if (!found) {
		ConfigurationItem* newCI = new ConfigurationItem;
		newCI->name = inName.substr(0, pos);
		newCI->parent = this;

		if (isElement)
			newCI->path = path + "/" + newCI->name;
		else
			newCI->path = path + "." + newCI->name;

		newCI->level = level + 1;
		if (pos == -1) {
			newCI->value = val;
			found = newCI;
		}
		else if (inName.at(pos) == '/') {
			found = newCI->AddChild(inName.substr(pos + 1), val, true);
		}
		else if (inName.at(pos) == '.')
			found = newCI->AddChild(inName.substr(pos + 1), val, false);

		if (isElement)
			children.push_back(newCI);
		else
			properties.push_back(newCI);

	}
	else {
		if (pos == -1)
			return nullptr;
		else if (inName.at(pos) == '/')
			found = found->AddChild(inName.substr(pos + 1), val, true);
		else if (inName.at(pos) == '.')
			found = found->AddChild(inName.substr(pos + 1), val, false);
	}

	return found;
}

ConfigurationItem* ConfigurationItem::FindProperty(const std::string& inName) {
	for (auto &prop : properties)
		if (prop->Match(inName))
			return prop;

	return nullptr;
}

ConfigurationManager::ConfigurationManager() {
}

ConfigurationManager::~ConfigurationManager() {
	Clear();
}

void ConfigurationManager::Clear() {
	for (auto &ci : ciList)
		delete ci;

	ciList.clear();
}

int ConfigurationManager::LoadConfig(const std::string& pathToFile, const std::string& rootElement) {
	XMLDocument configXML;
	configXML.LoadFile(pathToFile.c_str());

	if (configXML.Error())
		return 1;

	Clear();

	XMLElement* root = configXML.FirstChildElement(rootElement.c_str());
	if (!root)
		return 2;

	XMLNode* child = root->FirstChild();
	while (child) {
		if (child->ToComment())
			ciList.push_back(new ConfigurationItem(child->ToComment()->Value(), 0, 0));
		else if (child->ToElement())
			ciList.push_back(new ConfigurationItem(child->ToElement()));

		child = child->NextSibling();
	}
	return 0;
}

int ConfigurationManager::EnumerateCIs(std::vector<ConfigurationItem*>& outList, bool withProperties, bool traverse) {
	if (ciList.size() == 0)
		return 0;

	int count = 0;
	for (auto &ci : ciList) {
		outList.push_back(ci);
		count++;
		if (traverse) {
			if (withProperties)
				count += ci->EnumerateProperties(outList);
			count += ci->EnumerateChildren(outList, withProperties, traverse);
		}
	}
	return count;
}

int ConfigurationManager::EnumerateChildCIs(std::vector<ConfigurationItem*>& outList, const std::string& parentCI, bool withProperties, bool traverse) {
	ConfigurationItem* ci = FindCI(parentCI);
	if (!ci)
		return 0;

	return ci->EnumerateChildren(outList, withProperties, traverse);
}

bool ConfigurationManager::Exists(const std::string& name) {
	ConfigurationItem* itemFound = FindCI(name);
	if (itemFound)
		return true;
	else
		return false;
}

ConfigurationItem* ConfigurationManager::FindCI(const std::string& inName) {
	ConfigurationItem* found = nullptr;
	int pos = inName.find_first_of("/.");

	if (pos != -1) {
		std::string tmpName = inName.substr(0, pos);
		for (auto &ci : ciList)
			if (ci->Match(tmpName))
				found = ci;

		if (!found)
			return nullptr;

		if (inName.at(pos) == '/')
			found = found->FindChild(inName.substr(pos + 1));
		else if (inName.at(pos) == '.')
			found = found->FindProperty(inName.substr(pos + 1));

		if (!found)
			return nullptr;
	}
	else
		for (auto &ci : ciList)
			if (ci->Match(inName))
				found = ci;

	return found;
}

int ConfigurationManager::GetIntValue(const std::string& inName, int def) {
	int res = def;

	ConfigurationItem* itemFound = FindCI(inName);
	if (itemFound)
		if (!itemFound->value.empty())
			res = atoi(itemFound->value.c_str());

	return res;
}

float ConfigurationManager::GetFloatValue(const std::string& inName, float def) {
	float res = def;

	ConfigurationItem* itemFound = FindCI(inName);
	if (itemFound)
		if (!itemFound->value.empty())
			res = (float)atof(itemFound->value.c_str());

	return res;
}

std::string ConfigurationManager::GetString(const std::string& inName) {
	ConfigurationItem* itemFound = FindCI(inName);
	if (itemFound)
		return itemFound->value;

	return "";
}

void ConfigurationManager::SetDefaultValue(const std::string& inName, const std::string& newValue) {
	if (FindCI(inName))
		return;

	SetValue(inName, newValue, true);
}

void ConfigurationManager::SetDefaultValue(const std::string& inName, int newValue) {
	if (FindCI(inName))
		return;

	SetValue(inName, newValue, true);
}

void ConfigurationManager::SetValue(const std::string& inName, const std::string& newValue, bool flagDefault) {
	std::string search = inName;

	ConfigurationItem* itemFound = FindCI(search);
	if (itemFound) {
		itemFound->value = newValue;
		itemFound->isDefault = flagDefault;
	}
	else {
		int pos = search.find_first_of("/.");
		std::string tmpName = search.substr(0, pos);
		for (auto &ci : ciList) {
			if (ci->Match(tmpName))
				itemFound = ci;
		}

		if (!itemFound) {
			ConfigurationItem* newCI = new ConfigurationItem();
			newCI->name = tmpName;
			newCI->path = tmpName;
			ciList.push_back(newCI);
			itemFound = newCI;
			if (pos == -1) {
				newCI->value = newValue;
				newCI->isDefault = flagDefault;
				return;
			}
		}

		if (search.at(pos) == '/')
			itemFound->AddChild(search.substr(pos + 1), newValue)->isDefault = flagDefault;
		else
			itemFound->AddChild(search.substr(pos + 1), newValue, false)->isDefault = flagDefault;
	}
}

void ConfigurationManager::SetValue(const std::string& inName, int newValue, bool flagDefault) {
	char intStr[24];
	_snprintf_s(intStr, 24, 24, "%d", newValue);
	SetValue(inName, std::string(intStr), flagDefault);
}

void ConfigurationManager::SetValue(const std::string& inName, float newValue, bool flagDefault) {
	char intStr[24];
	_snprintf_s(intStr, 24, 24, "%0.5f", newValue);
	SetValue(inName, std::string(intStr), flagDefault);
}

bool ConfigurationManager::MatchValue(const std::string& inName, const std::string& val, bool useCase) {
	ConfigurationItem* itemFound = FindCI(inName);
	if (itemFound) {
		if (!useCase) {
			if (!_stricmp(itemFound->value.c_str(), val.c_str()))
				return true;
		}
		else {
			if (!strcmp(itemFound->value.c_str(), val.c_str()))
				return true;
		}
	}
	return false;
}

void ConfigurationManager::GetFullKey(ConfigurationItem* from, std::string& outStr) {
	std::vector<std::string> stringStack;
	outStr = "";

	while (from) {
		stringStack.push_back(from->name);
		from = from->parent;
	}

	int begin = stringStack.size() - 1;
	for (int i = begin; i >= 0; --i) {
		if (i != begin)
			outStr += "/";
		outStr += stringStack[i];
	}
}

int ConfigurationManager::GetValueArray(const std::string& containerName, const std::string& arrayName, std::vector<std::string>& outValues) {
	int count = 0;

	ConfigurationItem* container = FindCI(containerName);
	if (container)
		count = container->EnumerateChildren(arrayName, outValues);

	return count;
}

int ConfigurationManager::GetValueAttributeArray(const std::string& containerName, const std::string& arrayName, const std::string& attributeName, std::vector<std::string>& outValues) {
	int count = 0;

	ConfigurationItem* container = FindCI(containerName);
	if (container)
		count = container->EnumerateChildrenProperty(arrayName, attributeName, outValues);

	return count;
}

void ConfigurationManager::ReplaceVars(std::string & inoutStr)
{
	size_t first = inoutStr.find('%');
	size_t second;
	while (first != std::string::npos) {
		second = inoutStr.find('%', first + 1);
		size_t len = second - first;
		if (len > 1) {
			std::string varname = inoutStr.substr(first + 1, len - 1);
			inoutStr.replace(first, len+1, GetString(varname));
			first = inoutStr.find('%', first);
		}
		else if (len == 1) {
			inoutStr.replace(first, len + 1, "%");
			first = inoutStr.find('%', first+2);
		}
		else {
			break;
		}
	}
}

int ConfigurationManager::SaveConfig(const std::string& pathToFile, const std::string& rootElementName) {
	if (rootElementName.empty())
		return 1;

	XMLDocument doc;
	if (doc.LoadFile(pathToFile.c_str()) != XML_SUCCESS)
		return 2;

	doc.Clear();

	XMLElement* newElement = doc.NewElement(rootElementName.c_str());
	XMLElement* root = doc.InsertEndChild(newElement)->ToElement();

	for (auto &ci : ciList) {
		if (ci->isDefault)
			continue;
		if (ci->isComment) {
			XMLComment* newComment = doc.NewComment(ci->value.c_str());
			root->InsertEndChild(newComment);
		}
		else
			ci->ToXML(root);
	}

	if (doc.SaveFile(pathToFile.c_str()) != XML_SUCCESS)
		return 3;

	return 0;
}
