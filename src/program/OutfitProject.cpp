/*
BodySlide and Outfit Studio
Copyright (C) 2017  Caliente & ousnius
See the included LICENSE file
*/

#include "OutfitProject.h"
#include "../files/TriFile.h"
#include "../files/FBXWrangler.h"
#include "../program/FBXImportDialog.h"

#include "../FSEngine/FSManager.h"
#include "../FSEngine/FSEngine.h"

#include <sstream>
#include <regex>

OutfitProject::OutfitProject(ConfigurationManager& inConfig, OutfitStudio* inOwner) : appConfig(inConfig) {
	morpherInitialized = false;
	owner = inOwner;
	std::string defSkelFile = Config["Anim/DefaultSkeletonReference"];
	LoadSkeletonReference(defSkelFile);

	mCopyRef = true;
	if (owner->targetGame == SKYRIM || owner->targetGame == SKYRIMSE)
		mGenWeights = true;
	else
		mGenWeights = false;
}

OutfitProject::~OutfitProject() {
	for (auto &cloth : clothData)
		delete cloth.second;
}

std::string OutfitProject::Save(const wxString& strFileName,
	const wxString& strOutfitName,
	const wxString& strDataDir,
	const wxString& strBaseFile,
	const wxString& strGamePath,
	const wxString& strGameFile,
	bool genWeights,
	bool copyRef) {

	owner->UpdateProgress(1, _("Checking destination..."));
	std::string errmsg = "";
	std::string outfit = strOutfitName;
	std::string baseFile = strBaseFile;
	std::string gameFile = strGameFile;

	ReplaceForbidden(outfit);
	ReplaceForbidden(baseFile);
	ReplaceForbidden(gameFile);

	SliderSet outSet;
	outSet.SetName(outfit);
	outSet.SetDataFolder(strDataDir.ToStdString());
	outSet.SetInputFile(baseFile);
	outSet.SetOutputPath(strGamePath.ToStdString());
	outSet.SetOutputFile(gameFile);
	outSet.SetGenWeights(genWeights);

	wxString ssFileName = strFileName;
	if (ssFileName.Find("SliderSets\\") == wxString::npos)
		ssFileName = ssFileName.Prepend("SliderSets\\");

	mFileName = ssFileName;
	mOutfitName = outfit;
	mDataDir = strDataDir;
	mBaseFile = baseFile;
	mGamePath = strGamePath;
	mGameFile = gameFile;
	mCopyRef = copyRef;
	mGenWeights = genWeights;

	std::vector<std::string> shapes;
	GetShapes(shapes);

	wxString curDir(wxGetCwd());
	wxString folder(wxString::Format("%s/%s/%s", curDir, "ShapeData", strDataDir));
	wxFileName::Mkdir(folder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

	int prog = 5;
	int step = 10 / shapes.size();
	owner->UpdateProgress(prog);

	if (copyRef && !baseShape.empty()) {
		// Add all the reference shapes to the target list.
		outSet.AddShapeTarget(baseShape, ShapeToTarget(baseShape));
		outSet.AddTargetDataFolder(ShapeToTarget(baseShape), activeSet.ShapeToDataFolder(baseShape));
		owner->UpdateProgress(prog += step, _("Adding reference shapes..."));
	}

	// Add all the outfit shapes to the target list.
	for (auto &s : shapes) {
		if (IsBaseShape(s))
			continue;

		outSet.AddShapeTarget(s, ShapeToTarget(s));

		// Reference only if not local folder
		std::string shapeDataFolder = activeSet.ShapeToDataFolder(s);
		if (shapeDataFolder != activeSet.GetDefaultDataFolder())
			outSet.AddTargetDataFolder(ShapeToTarget(s), activeSet.ShapeToDataFolder(s));

		owner->UpdateProgress(prog += step, _("Adding outfit shapes..."));
	}
	
	std::string osdFileName = baseFile.substr(0, baseFile.find_last_of('.')) + ".osd";

	if (activeSet.size() > 0) {
		// Copy the reference slider info and add the outfit data to them.
		int id;
		std::string targ;
		std::string targSlider;
		std::string targSliderData;

		prog = 10;
		step = 20 / activeSet.size();
		owner->UpdateProgress(prog);

		for (int i = 0; i < activeSet.size(); i++) {
			id = outSet.CopySlider(&activeSet[i]);
			outSet[id].Clear();
			if (copyRef && !baseShape.empty()) {
				targ = ShapeToTarget(baseShape);
				targSlider = activeSet[i].TargetDataName(targ);
				if (baseDiffData.GetDiffSet(targSlider) && baseDiffData.GetDiffSet(targSlider)->size() > 0) {
					if (activeSet[i].IsLocalData(targSlider)) {
						targSliderData = osdFileName + "\\" + targSlider;
						outSet[id].AddDataFile(targ, targSlider, targSliderData);
					}
					else {
						targSliderData = activeSet[i].DataFileName(targSlider);
						outSet[id].AddDataFile(targ, targSlider, targSliderData, false);
					}
				}
			}

			for (auto &s : shapes) {
				if (IsBaseShape(s))
					continue;

				targ = ShapeToTarget(s);
				targSlider = activeSet[i].TargetDataName(targ);
				if (targSlider.empty())
					targSlider = targ + outSet[i].name;

				if (morpher.GetResultDiffSize(s, activeSet[i].name) > 0) {
					std::string shapeDataFolder = activeSet.ShapeToDataFolder(s);
					if (shapeDataFolder == activeSet.GetDefaultDataFolder() || activeSet[i].IsLocalData(targSlider)) {
						targSliderData = osdFileName + "\\" + targSlider;
						outSet[i].AddDataFile(targ, targSlider, targSliderData);
					}
					else {
						targSliderData = activeSet[i].DataFileName(targSlider);
						outSet[i].AddDataFile(targ, targSlider, targSliderData, false);
					}
				}
			}
			owner->UpdateProgress(prog += step, _("Calculating slider data..."));
		}
	}

	std::string saveDataPath = "ShapeData\\" + strDataDir;
	SaveSliderData(saveDataPath + "\\" + osdFileName, copyRef);
	
	prog = 60;
	owner->UpdateProgress(prog, _("Creating slider set file..."));

	SliderSetFile ssf(ssFileName.ToStdString());
	if (ssf.fail()) {
		ssf.New(ssFileName.ToStdString());
		if (ssf.fail()) {
			errmsg = _("Failed to open or create slider set file: ") + ssFileName.ToStdString();
			return errmsg;
		}
	}

	auto it = strFileName.rfind('\\');
	if (it != std::string::npos) {
		wxString ssNewFolder(wxString::Format("%s/%s", curDir, strFileName.substr(0, it)));
		wxFileName::Mkdir(ssNewFolder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	}
	else {
		wxString ssNewFolder(wxString::Format("%s/%s", curDir, "SliderSets"));
		wxFileName::Mkdir(ssNewFolder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	}

	owner->UpdateProgress(61, _("Saving slider set file..."));
	ssf.UpdateSet(outSet);
	if (!ssf.Save()) {
		errmsg = _("Failed to write to slider set file: ") + ssFileName.ToStdString();
		return errmsg;
	}

	owner->UpdateProgress(70, _("Saving NIF file..."));

	std::string saveFileName = saveDataPath + "\\" + baseFile;

	if (workNif.IsValid()) {
		NifFile clone(workNif);

		ChooseClothData(clone);

		if (!copyRef && !baseShape.empty()) {
			clone.DeleteShape(baseShape);
			workAnim.WriteToNif(&clone, baseShape);
		}
		else
			workAnim.WriteToNif(&clone);

		clone.GetShapeList(shapes);

		for (auto &s : shapes)
			clone.UpdateSkinPartitions(s);

		clone.SetShapeOrder(owner->GetShapeList());
		clone.GetHeader().SetExportInfo("Exported using Outfit Studio.");

		if (clone.Save(saveFileName)) {
			errmsg = _("Failed to write base .nif file: ") + saveFileName;
			return errmsg;
		}
	}

	owner->ShowPartition();
	owner->UpdateProgress(100, _("Finished"));
	return errmsg;
}

bool OutfitProject::SaveSliderData(const wxString& fileName, bool copyRef) {
	std::vector<std::string> shapes;
	GetShapes(shapes);
	
	if (activeSet.size() > 0) {
		std::string targ;
		std::string targSlider;

		DiffDataSets osdDiffs;
		std::map<std::string, std::map<std::string, std::string>> osdNames;
		
		// Copy the changed reference slider data and add the outfit data to them.
		for (int i = 0; i < activeSet.size(); i++) {
			if (copyRef && !baseShape.empty()) {
				targ = ShapeToTarget(baseShape);
				targSlider = activeSet[i].TargetDataName(targ);
				if (baseDiffData.GetDiffSet(targSlider) && baseDiffData.GetDiffSet(targSlider)->size() > 0) {
					if (activeSet[i].IsLocalData(targSlider)) {
						std::unordered_map<ushort, Vector3>* diff = baseDiffData.GetDiffSet(targSlider);
						osdDiffs.LoadSet(targSlider, targ, *diff);
						osdNames[fileName.ToStdString()][targSlider] = targ;
					}
				}
			}

			for (auto &s : shapes) {
				if (IsBaseShape(s))
					continue;

				targ = ShapeToTarget(s);
				targSlider = activeSet[i].TargetDataName(targ);
				if (targSlider.empty())
					targSlider = targ + activeSet[i].name;

				if (morpher.GetResultDiffSize(s, activeSet[i].name) > 0) {
					std::string shapeDataFolder = activeSet.ShapeToDataFolder(s);
					if (shapeDataFolder == activeSet.GetDefaultDataFolder() || activeSet[i].IsLocalData(targSlider)) {
						std::unordered_map<ushort, Vector3> diff;
						morpher.GetRawResultDiff(s, activeSet[i].name, diff);
						osdDiffs.LoadSet(targSlider, targ, diff);
						osdNames[fileName.ToStdString()][targSlider] = targ;
					}
				}
			}
		}

		if (!osdDiffs.SaveData(osdNames))
			return false;
	}

	return true;
}

std::string OutfitProject::SliderSetName() {
	return activeSet.GetName();
}

std::string OutfitProject::SliderSetFileName() {
	return activeSet.GetInputFileName();
}

std::string OutfitProject::OutfitName() {
	return outfitName;
}

void OutfitProject::ReplaceForbidden(std::string& str, const char& replacer) {
	const std::string forbiddenChars = "\\/:*?\"<>|";

	std::transform(str.begin(), str.end(), str.begin(), [&forbiddenChars, &replacer](char c) {
		return forbiddenChars.find(c) != std::string::npos ? replacer : c;
	});
}

bool OutfitProject::ValidSlider(int index) {
	if (index >= 0 && index < activeSet.size())
		return true;
	return false;
}

bool OutfitProject::ValidSlider(const std::string& sliderName) {
	return activeSet.SliderExists(sliderName);
}

bool OutfitProject::AllSlidersZero() {
	for (int i = 0; i < activeSet.size(); i++)
		if (activeSet[i].curValue != 0.0f)
			return false;
	return true;
}

int OutfitProject::SliderCount() {
	return activeSet.size();
}

void OutfitProject::GetSliderList(std::vector<std::string>& sliderNames) {
	for (int i = 0; i < activeSet.size(); i++)
		sliderNames.push_back(activeSet[i].name);
}

std::string OutfitProject::GetSliderName(int index) {
	if (!ValidSlider(index))
		return "";
	return activeSet[index].name;
}

void OutfitProject::AddEmptySlider(const std::string& newName) {
	int sliderID = activeSet.CreateSlider(newName);
	activeSet[sliderID].bShow = true;

	if (!baseShape.empty()) {
		std::string target = ShapeToTarget(baseShape);
		std::string shapeSlider = target + newName;
		activeSet[sliderID].AddDataFile(target, shapeSlider, shapeSlider);
		activeSet.AddShapeTarget(baseShape, target);
		baseDiffData.AddEmptySet(shapeSlider, target);
	}
}

void OutfitProject::AddZapSlider(const std::string& newName, std::unordered_map<ushort, float>& verts, const std::string& shapeName) {
	std::unordered_map<ushort, Vector3> diffData;
	Vector3 moveVec(0.0f, 1.0f, 0.0f);
	for (auto &v : verts)
		diffData[v.first] = moveVec;

	std::string target = ShapeToTarget(shapeName);
	std::string shapeSlider = target + newName;

	int sliderID = activeSet.CreateSlider(newName);
	activeSet[sliderID].bZap = true;
	activeSet[sliderID].defBigValue = 0.0f;
	activeSet[sliderID].defSmallValue = 0.0f;

	if (IsBaseShape(shapeName)) {
		activeSet[sliderID].AddDataFile(target, shapeSlider, shapeSlider);
		activeSet.AddShapeTarget(shapeName, target);
		baseDiffData.AddEmptySet(shapeSlider, target);
		for (auto &i : diffData)
			baseDiffData.SumDiff(shapeSlider, target, i.first, i.second);
	}
	else
		morpher.SetResultDiff(shapeName, newName, diffData);
}

void OutfitProject::AddCombinedSlider(const std::string& newName) {
	std::vector<Vector3> verts;
	std::unordered_map<ushort, Vector3> diffData;

	std::vector<std::string> shapes;
	GetShapes(shapes);
	for (auto &s : shapes) {
		if (IsBaseShape(s))
			continue;

		diffData.clear();
		GetLiveVerts(s, verts);
		workNif.CalcShapeDiff(s, &verts, diffData);
		morpher.SetResultDiff(s, newName, diffData);
	}

	int sliderID = activeSet.CreateSlider(newName);
	if (!baseShape.empty()) {
		std::string target = ShapeToTarget(baseShape);
		std::string shapeSlider = target + newName;
		activeSet[sliderID].AddDataFile(target, shapeSlider, shapeSlider);
		baseDiffData.AddEmptySet(shapeSlider, target);
		GetLiveVerts(baseShape, verts);
		workNif.CalcShapeDiff(baseShape, &verts, diffData);
		for (auto &i : diffData)
			baseDiffData.SumDiff(shapeSlider, target, i.first, i.second);
	}
}

int OutfitProject::CreateNifShapeFromData(const std::string& shapeName, std::vector<Vector3>& v, std::vector<Triangle>& t, std::vector<Vector2>& uv, std::vector<Vector3>* norms) {
	std::string blankSkel;
	std::string defaultName = "New Outfit";

	if (owner->targetGame <= SKYRIM)
		blankSkel = "res\\SkeletonBlank_sk.nif";
	else if (owner->targetGame == FO4)
		blankSkel = "res\\SkeletonBlank_fo4.nif";
	else if (owner->targetGame == SKYRIMSE)
		blankSkel = "res\\SkeletonBlank_sse.nif";

	NifFile blank;
	blank.Load(blankSkel);
	if (!blank.IsValid()) {
		wxLogError("Could not load 'SkeletonBlank.nif' for importing data file.");
		wxMessageBox(_("Could not load 'SkeletonBlank.nif' for importing data file."), _("Import Data Error"), wxICON_ERROR, owner);
		return 2;
	}

	if (!workNif.IsValid())
		ImportNIF(blankSkel, true, defaultName);

	if (owner->targetGame <= SKYRIM) {
		NiTriShapeData* nifShapeData = new NiTriShapeData();
		nifShapeData->Create(&v, &t, &uv);
		if (norms) {
			nifShapeData->normals = (*norms);
			nifShapeData->SetNormals(true);
		}

		int shapeID = blank.GetHeader().AddBlock(nifShapeData);

		NiSkinData* nifSkinData = new NiSkinData();
		int skinID = blank.GetHeader().AddBlock(nifSkinData);

		NiSkinPartition* nifSkinPartition = new NiSkinPartition();
		int partID = blank.GetHeader().AddBlock(nifSkinPartition);

		BSDismemberSkinInstance* nifDismemberInst = new BSDismemberSkinInstance();
		int dismemberID = blank.GetHeader().AddBlock(nifDismemberInst);
		nifDismemberInst->SetDataRef(skinID);
		nifDismemberInst->SetSkinPartitionRef(partID);
		nifDismemberInst->SetSkeletonRootRef(0);

		BSShaderTextureSet* nifTexset = new BSShaderTextureSet(blank.GetHeader().GetVersion());

		int shaderID;
		BSLightingShaderProperty* nifShader = nullptr;
		BSShaderPPLightingProperty* nifShaderPP = nullptr;
		switch (owner->targetGame) {
		case FO3:
		case FONV:
			nifShaderPP = new BSShaderPPLightingProperty();
			shaderID = blank.GetHeader().AddBlock(nifShaderPP);
			nifShaderPP->SetTextureSetRef(blank.GetHeader().AddBlock(nifTexset));
			break;
		case SKYRIM:
		default:
			nifShader = new BSLightingShaderProperty(blank.GetHeader().GetVersion());
			shaderID = blank.GetHeader().AddBlock(nifShader);
			nifShader->SetTextureSetRef(blank.GetHeader().AddBlock(nifTexset));
		}

		NiTriShape* nifTriShape = new NiTriShape();
		blank.GetHeader().AddBlock(nifTriShape);
		if (owner->targetGame < SKYRIM)
			nifTriShape->propertyRefs.AddBlockRef(shaderID);
		else
			nifTriShape->SetShaderPropertyRef(shaderID);

		nifTriShape->SetName(shapeName);
		nifTriShape->SetDataRef(shapeID);
		nifTriShape->SetSkinInstanceRef(dismemberID);

		blank.SetDefaultPartition(shapeName);
	}
	else if (owner->targetGame == FO4) {
		BSTriShape* triShapeBase;
		std::string wetShaderName = "template/OutfitTemplate_Wet.bgsm";
		BSSubIndexTriShape* nifBSTriShape = new BSSubIndexTriShape();
		nifBSTriShape->Create(&v, &t, &uv, norms);
		blank.GetHeader().AddBlock(nifBSTriShape);

		BSSkinInstance* nifBSSkinInstance = new BSSkinInstance();
		int skinID = blank.GetHeader().AddBlock(nifBSSkinInstance);
		nifBSSkinInstance->SetTargetRef(workNif.GetRootNodeID());

		BSSkinBoneData* nifBoneData = new BSSkinBoneData();
		int boneID = blank.GetHeader().AddBlock(nifBoneData);
		nifBSSkinInstance->SetDataRef(boneID);
		nifBSTriShape->SetSkinInstanceRef(skinID);
		triShapeBase = nifBSTriShape;

		BSShaderTextureSet* nifTexset = new BSShaderTextureSet(blank.GetHeader().GetVersion());

		BSLightingShaderProperty* nifShader = new BSLightingShaderProperty(blank.GetHeader().GetVersion());
		int shaderID = blank.GetHeader().AddBlock(nifShader);
		nifShader->SetTextureSetRef(blank.GetHeader().AddBlock(nifTexset));
		nifShader->SetWetMaterialName(wetShaderName);

		triShapeBase->SetName(shapeName);
		triShapeBase->SetShaderPropertyRef(shaderID);
	}
	else {
		BSTriShape* triShape = new BSTriShape();
		triShape->Create(&v, &t, &uv, norms);
		blank.GetHeader().AddBlock(triShape);

		NiSkinData* nifSkinData = new NiSkinData();
		int skinID = blank.GetHeader().AddBlock(nifSkinData);

		NiSkinPartition* nifSkinPartition = new NiSkinPartition();
		int partID = blank.GetHeader().AddBlock(nifSkinPartition);

		BSDismemberSkinInstance* nifDismemberInst = new BSDismemberSkinInstance();
		int dismemberID = blank.GetHeader().AddBlock(nifDismemberInst);
		nifDismemberInst->SetDataRef(skinID);
		nifDismemberInst->SetSkinPartitionRef(partID);
		nifDismemberInst->SetSkeletonRootRef(0);
		triShape->SetSkinInstanceRef(dismemberID);
		triShape->SetSkinned(true);

		BSShaderTextureSet* nifTexset = new BSShaderTextureSet(blank.GetHeader().GetVersion());

		BSLightingShaderProperty* nifShader = new BSLightingShaderProperty(blank.GetHeader().GetVersion());
		int shaderID = blank.GetHeader().AddBlock(nifShader);
		nifShader->SetTextureSetRef(blank.GetHeader().AddBlock(nifTexset));

		triShape->SetName(shapeName);
		triShape->SetShaderPropertyRef(shaderID);

		blank.SetDefaultPartition(shapeName);
		blank.UpdateSkinPartitions(shapeName);
	}

	workNif.CopyGeometry(shapeName, blank, shapeName);
	SetTextures(shapeName);

	return 0;
}

std::string OutfitProject::SliderShapeDataName(int index, const std::string& shapeName) {
	if (!ValidSlider(index))
		return "";
	return activeSet.ShapeToDataName(index, shapeName);
}

bool OutfitProject::SliderClamp(int index) {
	if (!ValidSlider(index))
		return false;
	return activeSet[index].bClamp;
}

bool OutfitProject::SliderZap(int index) {
	if (!ValidSlider(index))
		return false;
	return activeSet[index].bZap;
}

bool OutfitProject::SliderUV(int index) {
	if (!ValidSlider(index))
		return false;
	return activeSet[index].bUV;
}

wxArrayString OutfitProject::SliderZapToggles(int index) {
	wxArrayString toggles;
	if (ValidSlider(index))
		for (auto &toggle : activeSet[index].zapToggles)
			toggles.Add(toggle);

	return toggles;
}

bool OutfitProject::SliderInvert(int index) {
	if (!ValidSlider(index))
		return false;
	return activeSet[index].bInvert;
}

bool OutfitProject::SliderHidden(int index) {
	if (!ValidSlider(index))
		return false;
	return activeSet[index].bHidden;
}

void OutfitProject::SetSliderZap(int index, bool zap) {
	if (!ValidSlider(index))
		return;
	activeSet[index].bZap = zap;
}

void OutfitProject::SetSliderZapToggles(int index, const wxArrayString& toggles) {
	if (!ValidSlider(index))
		return;

	std::vector<std::string> zapToggles;
	for (auto &s : toggles)
		zapToggles.push_back(s.ToStdString());

	activeSet[index].zapToggles = zapToggles;
}

void OutfitProject::SetSliderInvert(int index, bool inv) {
	if (!ValidSlider(index))
		return;
	activeSet[index].bInvert = inv;
}

void OutfitProject::SetSliderUV(int index, bool uv) {
	if (!ValidSlider(index))
		return;
	activeSet[index].bUV = uv;
}

void OutfitProject::SetSliderHidden(int index, bool hidden) {
	if (!ValidSlider(index))
		return;
	activeSet[index].bHidden = hidden;
}

void OutfitProject::SetSliderDefault(int index, int val, bool isHi) {
	if (!ValidSlider(index))
		return;

	if (!isHi)
		activeSet[index].defSmallValue = val;
	else
		activeSet[index].defBigValue = val;
}

void OutfitProject::SetSliderName(int index, const std::string& newName) {
	if (!ValidSlider(index))
		return;

	std::string oldName = activeSet[index].name;
	std::vector<std::string> shapes;
	GetShapes(shapes);
	for (auto &s : shapes) {
		std::string oldDT = s + oldName;
		std::string newDT = s + newName;

		if (IsBaseShape(s))
			baseDiffData.RenameSet(oldDT, newDT);
		else
			morpher.RenameResultDiffData(s, oldName, newName);

		activeSet[index].RenameData(oldDT, newDT);
		activeSet[index].SetLocalData(newDT);
	}

	activeSet[index].name = newName;
}

float& OutfitProject::SliderValue(int index) {
	return activeSet[index].curValue;
}

float& OutfitProject::SliderValue(const std::string& name) {
	return activeSet[name].curValue;
}

float OutfitProject::SliderDefault(int index, bool hi) {
	if (hi)
		return activeSet[index].defBigValue;

	return activeSet[index].defSmallValue;
}

bool& OutfitProject::SliderShow(int index) {
	return activeSet[index].bShow;
}

bool& OutfitProject::SliderShow(const std::string& sliderName) {
	return activeSet[sliderName].bShow;
}

int OutfitProject::SliderIndexFromName(const std::string& sliderName) {
	for (int i = 0; i < activeSet.size(); i++)
		if (activeSet[i].name == sliderName)
			return i;

	return -1;
}

void OutfitProject::NegateSlider(const std::string& sliderName, const std::string& shapeName) {
	std::string target = ShapeToTarget(shapeName);

	if (IsBaseShape(shapeName)) {
		std::string sliderData = activeSet[sliderName].TargetDataName(target);
		baseDiffData.ScaleDiff(sliderData, target, -1.0f);
	}
	else
		morpher.ScaleResultDiff(target, sliderName, -1.0f);
}

void OutfitProject::MaskAffected(const std::string& sliderName, const std::string& shapeName) {
	mesh* m = owner->glView->GetMesh(shapeName);
	if (!m)
		return;

	m->ColorChannelFill(0, 0.0f);

	if (IsBaseShape(shapeName)) {
		std::vector<ushort> outIndices;
		std::string target = ShapeToTarget(shapeName);

		std::string sliderData = activeSet[sliderName].TargetDataName(target);
		baseDiffData.GetDiffIndices(sliderData, target, outIndices);

		for (auto &i : outIndices)
			m->vcolors[i].x = 1.0f;
	}
	else {
		std::unordered_map<ushort, Vector3> outDiff;
		morpher.GetRawResultDiff(shapeName, sliderName, outDiff);

		for (auto &i : outDiff)
			m->vcolors[i.first].x = 1.0f;
	}

	m->QueueUpdate(mesh::UpdateType::VertexColors);
}

int OutfitProject::WriteMorphTRI(const std::string& triPath) {
	std::vector<std::string> shapes;
	GetShapes(shapes);

	DiffDataSets currentDiffs;
	activeSet.LoadSetDiffData(currentDiffs);

	TriFile tri;
	std::string triFilePath = triPath;

	for (auto &shape : shapes) {
		bool bIsOutfit = true;
		if (IsBaseShape(shape))
			bIsOutfit = false;

		for (int s = 0; s < activeSet.size(); s++) {
			if (!activeSet[s].bUV && !activeSet[s].bClamp && !activeSet[s].bZap) {
				MorphDataPtr morph = std::make_shared<MorphData>();
				morph->name = activeSet[s].name;

				std::vector<Vector3> verts;
				int shapeVertCount = GetVertexCount(shape);

				if (shapeVertCount > 0)
					verts.resize(shapeVertCount);
				else
					continue;
				
				std::string target = ShapeToTarget(shape);
				if (!bIsOutfit) {
					std::string dn = activeSet[s].TargetDataName(target);
					if (dn.empty())
						continue;

					currentDiffs.ApplyDiff(dn, target, 1.0f, &verts);
				}
				else
					morpher.ApplyResultToVerts(morph->name, target, &verts);

				int i = 0;
				for (auto &v : verts) {
					if (!v.IsZero(true))
						morph->offsets.emplace(i, v);
					i++;
				}

				if (morph->offsets.size() > 0)
					tri.AddMorph(shape, morph);
			}
		}
	}

	if (!tri.Write(triFilePath))
		return false;

	return true;
}

int OutfitProject::SaveSliderBSD(const std::string& sliderName, const std::string& shapeName, const std::string& fileName) {
	std::string target = ShapeToTarget(shapeName);

	if (IsBaseShape(shapeName)) {
		std::string sliderData = activeSet[sliderName].TargetDataName(target);
		baseDiffData.SaveSet(sliderData, target, fileName);
	}
	else
		morpher.SaveResultDiff(target, sliderName, fileName);

	return 0;
}

int OutfitProject::SaveSliderOBJ(const std::string& sliderName, const std::string& shapeName, const std::string& fileName) {
	std::string target = ShapeToTarget(shapeName);
	std::vector<Triangle> tris;
	const std::vector<Vector3>* verts = workNif.GetRawVertsForShape(shapeName);
	workNif.GetTrisForShape(shapeName, &tris);
	const std::vector<Vector2>* uvs = workNif.GetUvsForShape(shapeName);

	std::vector<Vector3> outVerts = *verts;

	if (IsBaseShape(shapeName)) {
		std::string sliderData = activeSet[sliderName].TargetDataName(target);
		baseDiffData.ApplyDiff(sliderData, target, 1.0f, &outVerts);
	}
	else
		morpher.ApplyResultToVerts(sliderName, target, &outVerts);

	ObjFile obj;
	obj.SetScale(Vector3(0.1f, 0.1f, 0.1f));
	obj.AddGroup(shapeName, outVerts, tris, *uvs);
	if (obj.Save(fileName))
		return 1;

	return 0;
}

void OutfitProject::SetSliderFromBSD(const std::string& sliderName, const std::string& shapeName, const std::string& fileName) {
	std::string target = ShapeToTarget(shapeName);
	if (IsBaseShape(shapeName)) {
		std::string sliderData = activeSet[sliderName].TargetDataName(target);
		baseDiffData.LoadSet(sliderData, target, fileName);
	}
	else {
		DiffDataSets tmpSet;
		tmpSet.LoadSet(sliderName, target, fileName);
		std::unordered_map<ushort, Vector3>* diff = tmpSet.GetDiffSet(sliderName);
		morpher.SetResultDiff(target, sliderName, (*diff));
	}
}

bool OutfitProject::SetSliderFromOBJ(const std::string& sliderName, const std::string& shapeName, const std::string& fileName) {
	std::string target = ShapeToTarget(shapeName);

	ObjFile obj;
	obj.LoadForNif(fileName);

	std::vector<std::string> groupNames;
	obj.GetGroupList(groupNames);

	int i = 0;
	int index = 0;
	for (auto &n : groupNames) {
		if (n == shapeName) {
			index = i;
			break;
		}
		i++;
	}

	std::vector<Vector3> objVerts;
	std::vector<Vector2> objUVs;
	obj.CopyDataForIndex(index, &objVerts, nullptr, &objUVs);

	std::unordered_map<ushort, Vector3> diff;
	if (activeSet[sliderName].bUV) {
		if (workNif.CalcUVDiff(shapeName, &objUVs, diff))
			return false;
	}
	else {
		if (workNif.CalcShapeDiff(shapeName, &objVerts, diff, 10.0f))
			return false;
	}

	if (IsBaseShape(shapeName)) {
		std::string sliderData = activeSet[sliderName].TargetDataName(target);
		baseDiffData.LoadSet(sliderData, target, diff);
	}
	else
		morpher.SetResultDiff(target, sliderName, diff);

	return true;
}

bool OutfitProject::SetSliderFromFBX(const std::string& sliderName, const std::string& shapeName, const std::string& fileName) {
	std::string target = ShapeToTarget(shapeName);

	FBXWrangler fbxw;
	std::string invalidBones;

	bool result = fbxw.ImportScene(fileName);
	if (!result)
		return 1;

	std::vector<std::string>shapes;
	fbxw.GetShapeNames(shapes);
	bool found = false;
	for (auto &s : shapes)
		if (s == shapeName)
			found = true;

	if (!found)
		return false;

	FBXShape* shape = fbxw.GetShape(shapeName);

	std::unordered_map<ushort, Vector3> diff;
	if (IsBaseShape(shapeName)) {
		if (workNif.CalcShapeDiff(shapeName, &shape->verts, diff, 1.0f))
			return false;

		std::string sliderData = activeSet[sliderName].TargetDataName(target);
		baseDiffData.LoadSet(sliderData, target, diff);
	}
	else {
		if (workNif.CalcShapeDiff(shapeName, &shape->verts, diff, 1.0f))
			return false;

		morpher.SetResultDiff(target, sliderName, diff);
	}

	return true;
}

void OutfitProject::SetSliderFromDiff(const std::string& sliderName, const std::string& shapeName, std::unordered_map<ushort, Vector3>& diff) {
	std::string target = ShapeToTarget(shapeName);
	if (IsBaseShape(shapeName)) {
		std::string sliderData = activeSet[sliderName].TargetDataName(target);
		baseDiffData.LoadSet(sliderData, target, diff);
	}
	else {
		morpher.EmptyResultDiff(target, sliderName);
		morpher.SetResultDiff(target, sliderName, diff);
	}
}

int OutfitProject::GetVertexCount(const std::string& shapeName) {
	if (workNif.IsValid())
		return workNif.GetVertCountForShape(shapeName);

	return -1;
}

void OutfitProject::GetLiveVerts(const std::string& shapeName, std::vector<Vector3>& outVerts, std::vector<Vector2>* outUVs) {
	workNif.GetVertsForShape(shapeName, outVerts);
	if (outUVs)
		workNif.GetUvsForShape(shapeName, *outUVs);

	std::string target = ShapeToTarget(shapeName);
	if (IsBaseShape(shapeName)) {
		for (int i = 0; i < activeSet.size(); i++) {
			if (activeSet[i].bShow && activeSet[i].curValue != 0.0f) {
				std::string targetData = activeSet.ShapeToDataName(i, shapeName);
				if (targetData == "")
					continue;

				if (activeSet[i].bUV) {
					if (outUVs)
						baseDiffData.ApplyUVDiff(targetData, target, activeSet[i].curValue, outUVs);
				}
				else
					baseDiffData.ApplyDiff(targetData, target, activeSet[i].curValue, &outVerts);
			}
		}
	}
	else {
		for (int i = 0; i < activeSet.size(); i++) {
			if (activeSet[i].bShow && activeSet[i].curValue != 0.0f) {
				if (activeSet[i].bUV) {
					if (outUVs)
						morpher.ApplyResultToUVs(activeSet[i].name, target, outUVs, activeSet[i].curValue);
				}
				else
					morpher.ApplyResultToVerts(activeSet[i].name, target, &outVerts, activeSet[i].curValue);
			}
		}
	}
}

const std::string& OutfitProject::ShapeToTarget(const std::string& shapeName) {
	for (auto it = activeSet.TargetShapesBegin(); it != activeSet.TargetShapesEnd(); ++it)
		if (it->second == shapeName)
			return it->first;

	return shapeName;
}

void OutfitProject::GetShapes(std::vector<std::string>& outShapeNames) {
	workNif.GetShapeList(outShapeNames);
}

void OutfitProject::GetActiveBones(std::vector<std::string>& outBoneNames) {
	AnimSkeleton::getInstance().GetActiveBoneNames(outBoneNames);
}

std::vector<std::string> OutfitProject::GetShapeTextures(const std::string& shapeName) {
	if (shapeTextures.find(shapeName) != shapeTextures.end())
		return shapeTextures[shapeName];

	return std::vector<std::string>();
}

bool OutfitProject::GetShapeMaterialFile(const std::string& shapeName, MaterialFile& outMatFile) {
	if (shapeMaterialFiles.find(shapeName) != shapeMaterialFiles.end()) {
		outMatFile = shapeMaterialFiles[shapeName];
		return true;
	}

	return false;
}

void OutfitProject::SetTextures() {
	std::vector<std::string> shapes;
	GetShapes(shapes);
	for (auto &s : shapes)
		SetTextures(s);
}

void OutfitProject::SetTextures(const std::vector<std::string>& textureFiles) {
	std::vector<std::string> shapes;
	GetShapes(shapes);
	for (auto &s : shapes)
		SetTextures(s, textureFiles);
}

void OutfitProject::SetTextures(const std::string& shapeName, const std::vector<std::string>& textureFiles) {
	if (shapeName.empty())
		return;

	if (textureFiles.empty()) {
		std::string texturesDir = appConfig["GameDataPath"];
		bool hasMat = false;
		wxString matFile;

		const byte MAX_TEXTURE_PATHS = 10;
		std::vector<std::string> texFiles(MAX_TEXTURE_PATHS);

		NiShader* shader = workNif.GetShader(shapeName);
		if (shader) {
			// Find material file
			if (workNif.GetHeader().GetVersion().User() == 12 && workNif.GetHeader().GetVersion().User2() >= 130) {
				matFile = shader->GetName();
				if (!matFile.IsEmpty())
					hasMat = true;
			}
		}

		MaterialFile mat(MaterialFile::BGSM);
		if (hasMat) {
			matFile = matFile.Lower();
			matFile.Replace("\\", "/");

			// Attempt to read loose material file
			mat = MaterialFile(texturesDir + matFile.ToStdString());

			if (mat.Failed()) {
				// Search for material file in archives
				wxMemoryBuffer data;
				for (FSArchiveFile *archive : FSManager::archiveList()) {
					if (archive) {
						if (archive->hasFile(matFile.ToStdString())) {
							wxMemoryBuffer outData;
							archive->fileContents(matFile.ToStdString(), outData);

							if (!outData.IsEmpty()) {
								data = std::move(outData);
								break;
							}
						}
					}
				}

				if (!data.IsEmpty()) {
					std::string content((char*)data.GetData(), data.GetDataLen());
					std::istringstream contentStream(content, std::istringstream::binary);

					mat = MaterialFile(contentStream);
				}
			}

			if (!mat.Failed()) {
				if (mat.signature == MaterialFile::BGSM) {
					texFiles[0] = mat.diffuseTexture.c_str();
					texFiles[1] = mat.normalTexture.c_str();
					texFiles[4] = mat.envmapTexture.c_str();
					texFiles[5] = mat.glowTexture.c_str();
					texFiles[7] = mat.smoothSpecTexture.c_str();
				}
				else if (mat.signature == MaterialFile::BGEM) {
					texFiles[0] = mat.baseTexture.c_str();
					texFiles[1] = mat.fxNormalTexture.c_str();
					texFiles[4] = mat.fxEnvmapTexture.c_str();
					texFiles[5] = mat.envmapMaskTexture.c_str();
				}

				shapeMaterialFiles[shapeName] = std::move(mat);
			}
			else {
				for (int i = 0; i < MAX_TEXTURE_PATHS; i++)
					workNif.GetTextureForShape(shapeName, texFiles[i], i);
			}
		}
		else {
			for (int i = 0; i < MAX_TEXTURE_PATHS; i++)
				workNif.GetTextureForShape(shapeName, texFiles[i], i);
		}

		for (int i = 0; i < MAX_TEXTURE_PATHS; i++) {
			if (!texFiles[i].empty()) {
				texFiles[i] = std::regex_replace(texFiles[i], std::regex("/+|\\\\+"), "\\");													// Replace multiple slashes or forward slashes with one backslash
				texFiles[i] = std::regex_replace(texFiles[i], std::regex("^(.*?)\\\\textures\\\\", std::regex_constants::icase), "");			// Remove everything before the first occurence of "\textures\"
				texFiles[i] = std::regex_replace(texFiles[i], std::regex("^\\\\+"), "");														// Remove all backslashes from the front
				texFiles[i] = std::regex_replace(texFiles[i], std::regex("^(?!^textures\\\\)", std::regex_constants::icase), "textures\\");		// If the path doesn't start with "textures\", add it to the front

				texFiles[i] = texturesDir + texFiles[i];
			}
		}

		shapeTextures[shapeName] = texFiles;
	}
	else
		shapeTextures[shapeName] = textureFiles;
}

bool OutfitProject::IsValidShape(const std::string& shapeName) {
	std::vector<std::string> shapes;
	GetShapes(shapes);
	for (auto &s : shapes)
		if (s == shapeName)
			return true;

	return false;
}

void OutfitProject::RefreshMorphShape(const std::string& shapeName) {
	morpher.UpdateMeshFromNif(workNif, shapeName);
}

void OutfitProject::UpdateShapeFromMesh(const std::string& shapeName, const mesh* m) {
	std::vector<Vector3> liveVerts;
	for (int i = 0; i < m->nVerts; i++)
		liveVerts.emplace_back(std::move(Vector3(m->verts[i].x * -10, m->verts[i].z * 10, m->verts[i].y * 10)));

	workNif.SetVertsForShape(shapeName, liveVerts);
}

void OutfitProject::UpdateMorphResult(const std::string& shapeName, const std::string& sliderName, std::unordered_map<ushort, Vector3>& vertUpdates) {
	// Morph results are stored in two different places depending on whether it's an outfit or the base shape.
	// The outfit morphs are stored in the automorpher, whereas the base shape diff info is stored in directly in basediffdata.
	
	std::string target = ShapeToTarget(shapeName);
	std::string dataName = activeSet[sliderName].TargetDataName(target);
	if (!vertUpdates.empty()) {
		if (dataName.empty())
			activeSet[sliderName].AddDataFile(target, target + sliderName, target + sliderName);
		else
			activeSet[sliderName].SetLocalData(dataName);
	}

	if (IsBaseShape(shapeName)) {
		for (auto &i : vertUpdates) {
			Vector3 diffscale = Vector3(i.second.x * -10, i.second.z * 10, i.second.y * 10);
			baseDiffData.SumDiff(dataName, target, i.first, diffscale);
		}
	}
	else
		morpher.UpdateResultDiff(shapeName, sliderName, vertUpdates);
}

void OutfitProject::ScaleMorphResult(const std::string& shapeName, const std::string& sliderName, float scaleValue) {
	if (IsBaseShape(shapeName)) {
		std::string target = ShapeToTarget(shapeName);
		std::string dataName = activeSet[sliderName].TargetDataName(target);
		baseDiffData.ScaleDiff(dataName, target, scaleValue);
	}
	else
		morpher.ScaleResultDiff(shapeName, sliderName, scaleValue);
}

void OutfitProject::MoveVertex(const std::string& shapeName, const Vector3& pos, const int& id) {
	workNif.MoveVertex(shapeName, pos, id);
}

void OutfitProject::OffsetShape(const std::string& shapeName, const Vector3& xlate, std::unordered_map<ushort, float>* mask) {
	workNif.OffsetShape(shapeName, xlate, mask);
}

void OutfitProject::ScaleShape(const std::string& shapeName, const Vector3& scale, std::unordered_map<ushort, float>* mask) {
	workNif.ScaleShape(shapeName, scale, mask);
}

void OutfitProject::RotateShape(const std::string& shapeName, const Vector3& angle, std::unordered_map<ushort, float>* mask) {
	workNif.RotateShape(shapeName, angle, mask);
}

void OutfitProject::CopyBoneWeights(const std::string& destShape, const float& proximityRadius, const int& maxResults, std::unordered_map<ushort, float>* mask, std::vector<std::string>* inBoneList) {
	if (baseShape.empty())
		return;

	std::vector<std::string> lboneList;
	std::vector<std::string>* boneList;

	owner->UpdateProgress(1, _("Gathering bones..."));

	if (!inBoneList) {
		for (auto &bn : workAnim.shapeBones[baseShape])
			lboneList.push_back(bn);

		boneList = &lboneList;
	}
	else
		boneList = inBoneList;

	if (boneList->size() <= 0) {
		owner->UpdateProgress(90);
		return;
	}

	DiffDataSets dds;
	std::unordered_map<ushort, float> weights;
	for (auto &bone : *boneList) {
		weights.clear();
		dds.AddEmptySet(bone + "_WT_", "Weight");
		workAnim.GetWeights(baseShape, bone, weights);
		for (auto &w : weights) {
			Vector3 tmp;
			tmp.y = w.second;
			dds.UpdateDiff(bone + "_WT_", "Weight", w.first, tmp);
		}
	}

	owner->UpdateProgress(10, _("Initializing proximity data..."));

	InitConform();
	morpher.LinkRefDiffData(&dds);
	morpher.BuildProximityCache(destShape, proximityRadius);

	int step = 40 / boneList->size();
	int prog = 40;
	owner->UpdateProgress(prog);

	for (auto &boneName : *boneList) {
		std::string wtSet = boneName + "_WT_";
		morpher.GenerateResultDiff(destShape, wtSet, wtSet, maxResults);

		std::unordered_map<ushort, Vector3> diffResult;
		morpher.GetRawResultDiff(destShape, wtSet, diffResult);

		std::unordered_map<ushort, float> oldWeights;
		if (mask) {
			weights.clear();
			oldWeights.clear();

			workAnim.GetWeights(destShape, boneName, oldWeights);
		}

		for (auto &dr : diffResult) {
			if (mask)
				weights[dr.first] = dr.second.y * (1.0f - (*mask)[dr.first]);
			else
				weights[dr.first] = dr.second.y;
		}

		// Restore old weights from mask
		if (mask) {
			for (auto &w : oldWeights)
				if ((*mask)[w.first] > 0.0f)
					weights[w.first] = w.second;
		}

		if (diffResult.size() > 0) {
			if (workAnim.AddShapeBone(destShape, boneName)) {
				if (owner->targetGame == FO4) {
					// Fallout 4 bone transforms are stored in a bonedata structure per shape versus the node transform in the skeleton data.
					SkinTransform xForm;
					workNif.GetShapeBoneTransform(baseShape, boneName, xForm);
					workAnim.SetShapeBoneXForm(destShape, boneName, xForm);
				}
				else {
					SkinTransform xForm;
					workAnim.GetBoneXForm(boneName, xForm);
					workAnim.SetShapeBoneXForm(destShape, boneName, xForm);
				}
			}
		}

		workAnim.SetWeights(destShape, boneName, weights);
		owner->UpdateProgress(prog += step, _("Copying bone weights..."));
	}

	morpher.UnlinkRefDiffData();
	owner->UpdateProgress(90);
}

void OutfitProject::TransferSelectedWeights(const std::string& destShape, std::unordered_map<ushort, float>* mask, std::vector<std::string>* inBoneList) {
	if (baseShape.empty())
		return;

	owner->UpdateProgress(10, _("Gathering bones..."));

	std::vector<std::string>* boneList;
	std::vector<std::string> allBoneList;
	if (!inBoneList) {
		for (auto &boneName : workAnim.shapeBones[baseShape])
			allBoneList.push_back(boneName);

		boneList = &allBoneList;
	}
	else
		boneList = inBoneList;

	if (boneList->size() <= 0) {
		owner->UpdateProgress(100, _("Finished"));
		return;
	}

	int step = 50 / boneList->size();
	int prog = 40;
	owner->UpdateProgress(prog, _("Transferring bone weights..."));

	std::unordered_map<ushort, float> weights;
	std::unordered_map<ushort, float> oldWeights;
	for (auto &boneName : *boneList) {
		weights.clear();
		oldWeights.clear();
		workAnim.GetWeights(baseShape, boneName, weights);
		workAnim.GetWeights(destShape, boneName, oldWeights);

		for (auto &w : weights) {
			if (mask) {
				if (1.0f - (*mask)[w.first] > 0.0f)
					weights[w.first] = w.second * (1.0f - (*mask)[w.first]);
				else
					weights[w.first] = oldWeights[w.first];
			}
			else
				weights[w.first] = w.second;
		}

		if (workAnim.AddShapeBone(destShape, boneName)) {
			if (owner->targetGame == FO4) {
				// Fallout 4 bone transforms are stored in a bonedata structure per shape versus the node transform in the skeleton data.
				SkinTransform xForm;
				workNif.GetShapeBoneTransform(baseShape, boneName, xForm);
				workAnim.SetShapeBoneXForm(destShape, boneName, xForm);
			}
			else {
				SkinTransform xForm;
				workAnim.GetBoneXForm(boneName, xForm);
				workAnim.SetShapeBoneXForm(destShape, boneName, xForm);
			}
		}

		workAnim.SetWeights(destShape, boneName, weights);
		owner->UpdateProgress(prog += step, "");
	}

	owner->UpdateProgress(100, _("Finished"));
}

bool OutfitProject::HasUnweighted() {
	std::vector<std::string> shapes;
	GetShapes(shapes);
	for (auto &s : shapes) {
		if (!workNif.IsShapeSkinned(s))
			continue;

		std::vector<Vector3> verts;
		workNif.GetVertsForShape(s, verts);

		std::unordered_map<int, int> influences;
		for (int i = 0; i < verts.size(); i++)
			influences.emplace(i, 0);

		std::unordered_map<ushort, float> boneWeights;
		if (workAnim.shapeBones.find(s) != workAnim.shapeBones.end()) {
			for (auto &b : workAnim.shapeBones[s]) {
				boneWeights.clear();
				workAnim.GetWeights(s, b, boneWeights);
				for (int i = 0; i < verts.size(); i++) {
					auto id = boneWeights.find(i);
					if (id != boneWeights.end())
						influences.at(i)++;
				}
			}
		}

		mesh* m = owner->glView->GetMesh(s);
		bool unweighted = false;
		for (auto &i : influences) {
			if (i.second == 0) {
				if (!unweighted)
					m->ColorChannelFill(0, 0.0f);
				m->vcolors[i.first].x = 1.0f;
				unweighted = true;
			}
		}

		m->QueueUpdate(mesh::UpdateType::VertexColors);

		if (unweighted)
			return true;
	}
	return false;
}

void OutfitProject::ApplyBoneScale(const std::string& bone, int sliderPos, bool clear) {
	ClearBoneScale(false);

	std::vector<std::string> shapes;
	GetShapes(shapes);
	for (auto &s : shapes) {
		auto it = boneScaleVerts.find(s);
		if (it == boneScaleVerts.end()) {
			mesh* m = owner->glView->GetMesh(s);
			boneScaleVerts.emplace(s, std::vector<Vector3>(m->nVerts));
			it = boneScaleVerts.find(s);
			for (int i = 0; i < m->nVerts; i++)
				it->second[i] = std::move(Vector3(m->verts[i].x * -10, m->verts[i].z * 10, m->verts[i].y * 10));
		}

		std::vector<Vector3>* verts = &it->second;

		it = boneScaleOffsets.find(s);
		if (it == boneScaleOffsets.end())
			boneScaleOffsets.emplace(s, std::vector<Vector3>(verts->size()));
		it = boneScaleOffsets.find(s);

		for (auto &b : workAnim.shapeBones[s]) {
			if (b == bone) {
				std::vector<Vector3> boneRot;
				Vector3 boneTranslation;
				float boneScale;

				workNif.GetNodeTransform(b, boneRot, boneTranslation, boneScale);
				if (workWeights[s].empty())
					workAnim.GetWeights(s, b, workWeights[s]);

				for (auto &w : workWeights[s]) {
					Vector3 dir = (*verts)[w.first] - boneTranslation;
					dir.Normalize();
					Vector3 offset = dir * w.second * sliderPos / 5.0f;
					(*verts)[w.first] += offset;
					it->second[w.first] += offset;
				}
				break;
			}
		}

		if (clear)
			owner->glView->UpdateMeshVertices(s, verts, true, true, false);
		else
			owner->glView->UpdateMeshVertices(s, verts, true, false, false);
	}
}

void OutfitProject::ClearBoneScale(bool clear) {
	if (boneScaleOffsets.empty())
		return;

	std::vector<std::string> shapes;
	GetShapes(shapes);

	for (auto &s : shapes) {
		auto it = boneScaleVerts.find(s);
		std::vector<Vector3>* verts = &it->second;

		it = boneScaleOffsets.find(s);
		if (it != boneScaleOffsets.end()) {
			if (verts->size() == it->second.size()) {
				for (int i = 0; i < verts->size(); i++)
					(*verts)[i] -= it->second[i];

				if (clear)
					owner->glView->UpdateMeshVertices(s, verts, true, true, false);
				else
					owner->glView->UpdateMeshVertices(s, verts, false, false, false);
			}
		}
	}

	boneScaleVerts.clear();
	boneScaleOffsets.clear();
}

void OutfitProject::AddBoneRef(const std::string& boneName) {
	SkinTransform xForm;
	if (!AnimSkeleton::getInstance().GetSkinTransform(boneName, xForm, xForm))
		return;

	std::vector<std::string> shapes;
	GetShapes(shapes);
	for (auto &s : shapes)
		if (workAnim.AddShapeBone(s, boneName))
			workAnim.SetShapeBoneXForm(s, boneName, xForm);
}

void OutfitProject::AddCustomBoneRef(const std::string& boneName, const Vector3& translation) {
	AnimBone& customBone = AnimSkeleton::getInstance().AddBone(boneName, true);

	SkinTransform xForm;
	xForm.translation = translation;

	customBone.trans = xForm.translation;
	customBone.scale = xForm.scale;

	std::vector<std::string> shapes;
	GetShapes(shapes);
	for (auto &s : shapes)
		if (workAnim.AddShapeBone(s, boneName))
			workAnim.SetShapeBoneXForm(s, boneName, xForm);
}

void OutfitProject::ClearWorkSliders() {
	morpher.ClearResultDiff();
}

void OutfitProject::ClearReference() {
	DeleteShape(baseShape);

	if (activeSet.size() > 0)
		activeSet.Clear();
}

void OutfitProject::ClearOutfit() {
	std::vector<std::string> shapes;
	GetShapes(shapes);
	for (auto &s : shapes) {
		if (IsBaseShape(s))
			continue;

		DeleteShape(s);
	}
	ClearWorkSliders();
}

void OutfitProject::ClearSlider(const std::string& shapeName, const std::string& sliderName) {
	std::string target = ShapeToTarget(shapeName);

	if (IsBaseShape(shapeName)) {
		std::string data = activeSet[sliderName].TargetDataName(target);
		baseDiffData.EmptySet(data, target);
	}
	else
		morpher.EmptyResultDiff(target, sliderName);
}

void OutfitProject::ClearUnmaskedDiff(const std::string& shapeName, const std::string& sliderName, std::unordered_map<ushort, float>* mask) {
	std::string target = ShapeToTarget(shapeName);

	if (IsBaseShape(shapeName)) {
		std::string data = activeSet[sliderName].TargetDataName(target);
		baseDiffData.ZeroVertDiff(data, target, nullptr, mask);
	}
	else
		morpher.ZeroVertDiff(target, sliderName, nullptr, mask);
}

void OutfitProject::DeleteSlider(const std::string& sliderName) {
	std::vector<std::string> shapes;
	GetShapes(shapes);

	for (auto &s : shapes) {
		std::string target = ShapeToTarget(s);
		std::string data = activeSet[sliderName].TargetDataName(target);

		if (IsBaseShape(s))
			baseDiffData.ClearSet(data);
		else
			morpher.ClearResultSet(data);
	}

	activeSet.DeleteSlider(sliderName);
}

int OutfitProject::LoadSkeletonReference(const std::string& skeletonFileName) {
	return AnimSkeleton::getInstance().LoadFromNif(skeletonFileName);
}

int OutfitProject::LoadReferenceTemplate(const std::string& sourceFile, const std::string& set, const std::string& shape, bool mergeSliders) {
	if (sourceFile.empty() || set.empty()) {
		wxLogError("Template source entries are invalid.");
		wxMessageBox(_("Template source entries are invalid."), _("Reference Error"), wxICON_ERROR, owner);
		return 1;
	}

	return LoadReference(sourceFile, set, mergeSliders, shape);
}

int OutfitProject::LoadReferenceNif(const std::string& fileName, const std::string& shapeName, bool mergeSliders) {
	if (mergeSliders)
		DeleteShape(baseShape);
	else
		ClearReference();

	NifFile refNif;
	int error = refNif.Load(fileName);
	if (error) {
		if (error == 2) {
			wxString errorText = wxString::Format(_("NIF version not supported!\n\nFile: %s\n%s"),
				refNif.GetFileName(), refNif.GetHeader().GetVersion().GetVersionInfo());

			wxLogError(errorText);
			wxMessageBox(errorText, _("Reference Error"), wxICON_ERROR, owner);
			return 3;
		}

		wxLogError("Could not load reference NIF file '%s'!", fileName);
		wxMessageBox(wxString::Format(_("Could not load reference NIF file '%s'!"), fileName), _("Reference Error"), wxICON_ERROR, owner);
		return 2;
	}

	CheckNIFTarget(refNif);

	baseShape = shapeName;

	std::vector<std::string> shapes;
	GetShapes(shapes);
	for (auto &s : shapes) {
		if (IsBaseShape(s)) {
			std::string newName = s + "_ref";
			refNif.RenameShape(s, newName);
			baseShape = newName;
			break;
		}
	}

	if (workNif.IsValid()) {
		// Copy only reference shape
		workNif.CopyGeometry(baseShape, refNif, baseShape);
		workAnim.LoadFromNif(&workNif, baseShape);
	}
	else {
		// Copy the full file
		workNif.CopyFrom(refNif);
		workAnim.LoadFromNif(&workNif);

		// Delete all except for reference
		GetShapes(shapes);
		for (auto &s : shapes)
			if (s != baseShape)
				DeleteShape(s);
	}

	activeSet.LoadSetDiffData(baseDiffData);
	AutoOffset(workNif);
	return 0;
}

int OutfitProject::LoadReference(const std::string& fileName, const std::string& setName, bool mergeSliders, const std::string& shapeName) {
	if (mergeSliders)
		DeleteShape(baseShape);
	else
		ClearReference();

	std::string oldTarget;
	SliderSetFile sset(fileName);

	if (sset.fail()) {
		wxLogError("Could not load slider set file '%s'!", fileName);
		wxMessageBox(wxString::Format(_("Could not load slider set file '%s'!"), fileName), _("Reference Error"), wxICON_ERROR, owner);
		return 1;
	}

	std::string dataFolder = activeSet.GetDefaultDataFolder();
	std::vector<std::string> dataNames = activeSet.GetLocalData(shapeName);

	sset.GetSet(setName, activeSet);

	activeSet.SetBaseDataPath(Config["ShapeDataPath"]);
	std::string inMeshFile = activeSet.GetInputFileName();

	NifFile refNif;
	int error = refNif.Load(inMeshFile);
	if (error) {
		if (error == 2) {
			wxString errorText = wxString::Format(_("NIF version not supported!\n\nFile: %s\n%s"),
				refNif.GetFileName(), refNif.GetHeader().GetVersion().GetVersionInfo());

			wxLogError(errorText);
			wxMessageBox(errorText, _("Reference Error"), wxICON_ERROR, owner);
			ClearReference();
			return 5;
		}

		ClearReference();
		wxLogError("Could not load reference NIF file '%s'!", inMeshFile);
		wxMessageBox(wxString::Format(_("Could not load reference NIF file '%s'!"), inMeshFile), _("Reference Error"), wxICON_ERROR, owner);
		return 2;
	}

	CheckNIFTarget(refNif);

	std::vector<std::string> shapes;
	refNif.GetShapeList(shapes);
	if (shapes.empty()) {
		ClearReference();
		wxLogError("Reference NIF file '%s' does not contain any shapes.", refNif.GetFileName());
		wxMessageBox(wxString::Format(_("Reference NIF file '%s' does not contain any shapes."), refNif.GetFileName()), _("Reference Error"), wxICON_ERROR, owner);
		return 3;
	}

	std::string shape = shapeName;
	if (shape.empty())
		shape = shapes[0];

	GetShapes(shapes);
	for (auto &s : shapes) {
		if (s == shape) {
			std::string newName = s + "_ref";
			refNif.RenameShape(s, newName);
			shape = newName;
			break;
		}
	}

	int newVertCount = refNif.GetVertCountForShape(shape);
	if (newVertCount == -1) {
		ClearReference();
		wxLogError("Shape '%s' not found in reference NIF file '%s'!", shape, refNif.GetFileName());
		wxMessageBox(wxString::Format(_("Shape '%s' not found in reference NIF file '%s'!"), shape, refNif.GetFileName()), _("Reference Error"), wxICON_ERROR, owner);
		return 4;
	}

	// Add cloth data block of NIF to the list
	std::vector<BSClothExtraData*> clothDataBlocks = refNif.GetChildren<BSClothExtraData>(refNif.GetHeader().GetBlock<NiNode>(0), true);
	for (auto &cloth : clothDataBlocks)
		clothData[inMeshFile] = cloth->Clone();

	refNif.GetHeader().DeleteBlockByType("BSClothExtraData");

	if (workNif.IsValid()) {
		// Copy only reference shape
		workNif.CopyGeometry(shape, refNif, shape);
		workAnim.LoadFromNif(&workNif, shape);
	}
	else {
		// Copy the full file
		workNif.CopyFrom(refNif);
		workAnim.LoadFromNif(&workNif);

		// Delete all except for reference
		GetShapes(shapes);
		for (auto &s : shapes)
			if (s != shape)
				DeleteShape(s);
	}

	baseShape = shape;

	activeSet.LoadSetDiffData(baseDiffData);
	activeSet.SetReferencedData(baseShape);
	for (auto &dn : dataNames)
		activeSet.SetReferencedDataByName(baseShape, dn, true);

	// Keep default data folder from current project if existing
	if (!dataFolder.empty())
		activeSet.SetDataFolder(dataFolder);

	AutoOffset(workNif);
	return 0;
}

int OutfitProject::OutfitFromSliderSet(const std::string& fileName, const std::string& sliderSetName, std::vector<std::string>* origShapeOrder) {
	owner->StartProgress(_("Loading slider set..."));
	SliderSetFile InSS(fileName);
	if (InSS.fail()) {
		owner->EndProgress();
		return 1;
	}

	owner->UpdateProgress(20, _("Retrieving sliders..."));
	if (InSS.GetSet(sliderSetName, activeSet)) {
		owner->EndProgress();
		return 3;
	}

	activeSet.SetBaseDataPath(Config["ShapeDataPath"]);
	std::string inputNif = activeSet.GetInputFileName();

	owner->UpdateProgress(30, _("Loading outfit shapes..."));
	if (ImportNIF(inputNif, true, sliderSetName)) {
		owner->EndProgress();
		return 4;
	}

	if (origShapeOrder)
		workNif.GetShapeList(*origShapeOrder);

	std::string newBaseShape;

	// First external target with skin shader becomes reference
	std::vector<std::string> refTargets;
	activeSet.GetReferencedTargets(refTargets);
	for (auto &target : refTargets) {
		std::string shape = activeSet.TargetToShape(target);
		if (workNif.IsShaderSkin(shape)) {
			newBaseShape = shape;
			break;
		}
	}

	// No external target found, first skin shaded shape becomes reference
	if (refTargets.empty()) {
		for (auto shape = activeSet.TargetShapesBegin(); shape != activeSet.TargetShapesEnd(); ++shape) {
			if (workNif.IsShaderSkin(shape->second)) {
				newBaseShape = shape->second;
				break;
			}
		}
	}

	// Prevent duplication if valid reference was found
	DeleteShape(newBaseShape);
	baseShape = newBaseShape;

	owner->UpdateProgress(90, _("Updating slider data..."));
	morpher.LoadResultDiffs(activeSet);

	wxString rest;
	mFileName = fileName;
	if (mFileName.EndsWith(".xml", &rest))
		mFileName = rest.Append(".osp");

	mOutfitName = sliderSetName;
	mDataDir = activeSet.GetDefaultDataFolder();
	mBaseFile = activeSet.GetInputFileName();
	mBaseFile = mBaseFile.AfterLast('\\');

	mGamePath = activeSet.GetOutputPath();
	mGameFile = activeSet.GetOutputFile();
	mCopyRef = true;
	mGenWeights = activeSet.GenWeights();

	owner->UpdateProgress(100, _("Finished"));
	owner->EndProgress();
	return 0;
}

void OutfitProject::AutoOffset(NifFile& nif) {
	std::vector<std::string> shapes;
	nif.GetShapeList(shapes);

	for (auto &s : shapes) {
		SkinTransform xFormSkin;
		if (!nif.GetShapeBoneTransform(s, 0xFFFFFFFF, xFormSkin))
			continue;

		Matrix4 matSkinInv = xFormSkin.ToMatrix().Inverse();

		std::vector<Vector3> verts;
		nif.GetVertsForShape(s, verts);

		for (auto &v : verts)
			v = matSkinInv * v;

		SkinTransform xForm;
		nif.SetShapeBoneTransform(s, 0xFFFFFFFF, xForm);
		nif.ClearShapeTransform(s);

		nif.SetVertsForShape(s, verts);
	}

	nif.ClearRootTransform();
}

void OutfitProject::InitConform() {
	morpher.SetRef(workNif, baseShape);
	morpher.LinkRefDiffData(&baseDiffData);
	morpher.SourceShapesFromNif(workNif);
}

void OutfitProject::ConformShape(const std::string& shapeName) {
	if (!workNif.IsValid() || baseShape.empty())
		return;

	morpher.BuildProximityCache(shapeName);

	std::string refTarget = ShapeToTarget(baseShape);
	for (int i = 0; i < activeSet.size(); i++)
		if (SliderShow(i) && !SliderZap(i) && !SliderUV(i))
			morpher.GenerateResultDiff(shapeName, activeSet[i].name, activeSet[i].TargetDataName(refTarget));
}

void OutfitProject::DeleteVerts(const std::string& shapeName, const std::unordered_map<ushort, float>& mask) {
	std::vector<ushort> indices;
	indices.reserve(mask.size());

	for (auto &m : mask)
		indices.push_back(m.first);

	std::sort(indices.begin(), indices.end());
	indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

	bool shapeDeleted = workNif.DeleteVertsForShape(shapeName, indices);
	if (!shapeDeleted) {
		workAnim.DeleteVertsForShape(shapeName, indices);

		std::string target = ShapeToTarget(shapeName);
		if (IsBaseShape(shapeName))
			baseDiffData.DeleteVerts(target, indices);
		else
			morpher.DeleteVerts(target, indices);
		
		activeSet.SetReferencedData(shapeName, true);
	}
	else
		DeleteShape(shapeName);
}

void OutfitProject::DuplicateShape(const std::string& sourceShape, const std::string& destShape) {
	workNif.CopyGeometry(destShape, workNif, sourceShape);
	workAnim.LoadFromNif(&workNif, destShape);
}

void OutfitProject::DeleteShape(const std::string& shapeName) {
	workAnim.ClearShape(shapeName);
	workNif.DeleteShape(shapeName);
	owner->glView->DeleteMesh(shapeName);

	if (IsBaseShape(shapeName)) {
		morpher.UnlinkRefDiffData();
		baseShape.clear();
	}
}

void OutfitProject::RenameShape(const std::string& shapeName, const std::string& newShapeName) {
	workNif.RenameShape(shapeName, newShapeName);
	workAnim.RenameShape(shapeName, newShapeName);
	activeSet.RenameShape(shapeName, newShapeName);

	if (IsBaseShape(shapeName)) {
		activeSet.SetReferencedData(newShapeName, true);
		baseDiffData.DeepRename(shapeName, newShapeName);
		baseShape = newShapeName;
	}
	else
		morpher.RenameShape(shapeName, newShapeName);

	wxLogMessage("Renamed shape '%s' to '%s'.", shapeName, newShapeName);
}

void OutfitProject::UpdateNifNormals(NifFile* nif, const std::vector<mesh*>& shapeMeshes) {
	std::vector<Vector3> liveNorms;
	for (auto &m : shapeMeshes) {
		if (nif->IsShaderSkin(m->shapeName) && (owner->targetGame == SKYRIM || owner->targetGame == SKYRIMSE))
			continue;

		liveNorms.clear();
		for (int i = 0; i < m->nVerts; i++)
			liveNorms.emplace_back(std::move(Vector3(m->norms[i].x* -1, m->norms[i].z, m->norms[i].y)));

		nif->SetNormalsForShape(m->shapeName, liveNorms);
		nif->CalcTangentsForShape(m->shapeName);
	}
}

int OutfitProject::ImportNIF(const std::string& fileName, bool clear, const std::string& inOutfitName) {
	if (clear)
		ClearOutfit();

	if (fileName.empty()) {
		wxLogMessage("No outfit selected.");
		return 0;
	}

	if (!inOutfitName.empty())
		outfitName = inOutfitName;
	else if (outfitName.empty())
		outfitName = "New Outfit";

	if (clear) {
		wxFileName file(fileName);
		mGameFile = file.GetName();
		mGamePath = file.GetPath();

		int pos = mGamePath.Lower().Find("meshes");
		if (pos != wxNOT_FOUND)
			mGamePath = mGamePath.Mid(pos);
		else
			mGamePath.Clear();

		if (owner->targetGame == SKYRIM || owner->targetGame == SKYRIMSE) {
			wxString fileRest;
			if (mGameFile.EndsWith("_0", &fileRest) || mGameFile.EndsWith("_1", &fileRest))
				mGameFile = fileRest;
		}
	}

	NifFile nif;
	int error = nif.Load(fileName);
	if (error) {
		if (error == 2) {
			wxString errorText = wxString::Format(_("NIF version not supported!\n\nFile: %s\n%s"),
				nif.GetFileName(), nif.GetHeader().GetVersion().GetVersionInfo());

			wxLogError(errorText);
			wxMessageBox(errorText, _("NIF Error"), wxICON_ERROR, owner);
			return 4;
		}

		wxLogError("Could not load NIF file '%s'!", fileName);
		wxMessageBox(wxString::Format(_("Could not load NIF file '%s'!"), fileName), _("NIF Error"), wxICON_ERROR, owner);
		return 1;
	}

	CheckNIFTarget(nif);

	nif.SetNodeName(0, "Scene Root");

	std::vector<std::string> nifShapes;
	nif.GetShapeList(nifShapes);
	for (auto &s : nifShapes)
		nif.RenameDuplicateShape(s);

	if (!baseShape.empty())
		nif.RenameShape(baseShape, baseShape + "_outfit");

	std::vector<std::string> shapes;
	GetShapes(shapes);

	nif.GetShapeList(nifShapes);
	for (auto &s : nifShapes) {
		std::vector<std::string> uniqueShapes;
		nif.GetShapeList(uniqueShapes);
		uniqueShapes.insert(uniqueShapes.end(), shapes.begin(), shapes.end());

		std::string newName = s;
		int uniqueCount = 0;
		for (;;) {
			auto foundShape = find(uniqueShapes.begin(), uniqueShapes.end(), newName);
			if (foundShape != uniqueShapes.end()) {
				uniqueShapes.erase(foundShape);
				uniqueCount++;
				if (uniqueCount > 1)
					newName = s + wxString::Format("_%d", uniqueCount).ToStdString();
			}
			else {
				if (uniqueCount > 1)
					nif.RenameShape(s, newName);
				break;
			}
		}
	}

	AutoOffset(nif);

	// Add cloth data block of NIF to the list
	std::vector<BSClothExtraData*> clothDataBlocks = nif.GetChildren<BSClothExtraData>(nif.GetHeader().GetBlock<NiNode>(0), true);
	for (auto &cloth : clothDataBlocks)
		clothData[fileName] = cloth->Clone();

	nif.GetHeader().DeleteBlockByType("BSClothExtraData");

	nif.GetShapeList(nifShapes);
	if (workNif.IsValid()) {
		for (auto &s : nifShapes) {
			workNif.CopyGeometry(s, nif, s);
			workAnim.LoadFromNif(&workNif, s);
		}
	}
	else {
		workNif.CopyFrom(nif);
		workAnim.LoadFromNif(&workNif);
	}

	return 0;
}

int OutfitProject::ExportNIF(const std::string& fileName, const std::vector<mesh*>& modMeshes, bool writeNormals, bool withRef) {
	NifFile clone(workNif);

	ChooseClothData(clone);

	std::vector<Vector3> liveVerts;
	std::vector<Vector3> liveNorms;
	for (auto &m : modMeshes) {
		liveVerts.clear();
		liveNorms.clear();
		for (int i = 0; i < m->nVerts; i++) {
			liveVerts.emplace_back(std::move(Vector3(m->verts[i].x * -10, m->verts[i].z * 10, m->verts[i].y * 10)));
			liveNorms.emplace_back(std::move(Vector3(m->norms[i].x * -1, m->norms[i].z, m->norms[i].y)));
		}
		clone.SetVertsForShape(m->shapeName, liveVerts);

		if (writeNormals) {
			if (clone.IsShaderSkin(m->shapeName) && (owner->targetGame == SKYRIM || owner->targetGame == SKYRIMSE))
				continue;

			clone.SetNormalsForShape(m->shapeName, liveNorms);
			clone.CalcTangentsForShape(m->shapeName);
		}
	}

	if (!withRef && !baseShape.empty()) {
		clone.DeleteShape(baseShape);
		workAnim.WriteToNif(&clone, baseShape);
	}
	else
		workAnim.WriteToNif(&clone);

	std::vector<std::string> shapes;
	clone.GetShapeList(shapes);
	for (auto &s : shapes)
		clone.UpdateSkinPartitions(s);

	clone.SetShapeOrder(owner->GetShapeList());
	clone.GetHeader().SetExportInfo("Exported using Outfit Studio.");
	return clone.Save(fileName);
}


void OutfitProject::ChooseClothData(NifFile& nif) {
	if (!clothData.empty()) {
		wxArrayString clothFileNames;
		for (auto &cloth : clothData)
			clothFileNames.Add(cloth.first);

		wxMultiChoiceDialog clothDataChoice(owner, _("There was cloth physics data loaded at some point (BSClothExtraData). Please choose all the origins to use in the output."), _("Choose cloth data"), clothFileNames);
		if (clothDataChoice.ShowModal() == wxID_CANCEL)
			return;

		wxArrayInt sel = clothDataChoice.GetSelections();
		for (int i = 0; i < sel.Count(); i++) {
			std::string selString = clothFileNames[sel[i]].ToStdString();
			if (!selString.empty()) {
				auto clothBlock = clothData[selString]->Clone();
				int id = nif.GetHeader().AddBlock(clothBlock);
				if (id != 0xFFFFFFFF) {
					NiNode* root = nif.GetHeader().GetBlock<NiNode>(0);
					if (root)
						root->AddExtraDataRef(id);
				}
			}
		}
	}
}

int OutfitProject::ExportShapeNIF(const std::string& fileName, const std::vector<std::string>& exportShapes) {
	if (exportShapes.empty())
		return 1;

	if (!workNif.IsValid())
		return 2;

	NifFile clone(workNif);
	ChooseClothData(clone);

	std::vector<std::string> shapes;
	clone.GetShapeList(shapes);

	for (auto &s : shapes)
		if (find(exportShapes.begin(), exportShapes.end(), s) == exportShapes.end())
			clone.DeleteShape(s);

	clone.GetShapeList(shapes);
	for (auto &s : shapes)
		clone.UpdateSkinPartitions(s);

	clone.GetHeader().SetExportInfo("Exported using Outfit Studio.");
	return clone.Save(fileName);
}

int OutfitProject::ImportOBJ(const std::string& fileName, const std::string& shapeName, const std::string& mergeShape) {
	ObjFile obj;
	obj.SetScale(Vector3(10.0f, 10.0f, 10.0f));

	if (!shapeName.empty())
		outfitName = shapeName;
	else if (outfitName.empty())
		outfitName = "New Outfit";

	if (obj.LoadForNif(fileName)) {
		wxLogError("Could not load OBJ file '%s'!", fileName);
		wxMessageBox(wxString::Format(_("Could not load OBJ file '%s'!"), fileName), _("OBJ Error"), wxICON_ERROR, owner);
		return 1;
	}
	std::vector<std::string> objGroupNames;
	obj.GetGroupList(objGroupNames);
	for (int i = 0; i < objGroupNames.size(); i++) {
		std::vector<Vector3> v;
		std::vector<Triangle> t;
		std::vector<Vector2> uv;
		if (!obj.CopyDataForIndex(i, &v, &t, &uv)) {
			wxLogError("Could not copy data from OBJ file '%s'!", fileName);
			wxMessageBox(wxString::Format(_("Could not copy data from OBJ file '%s'!"), fileName), _("OBJ Error"), wxICON_ERROR, owner);
			return 3;
		}

		// Skip zero size groups.  
		if (v.size() == 0)
			continue;

		std::string useShapeName = objGroupNames[i];

		if (mergeShape != "") {
			std::vector<Vector3> shapeVerts;
			workNif.GetVertsForShape(mergeShape, shapeVerts);
			if (shapeVerts.size() == v.size()) {
				int ret = wxMessageBox(_("The vertex count of the selected .obj file matches the currently selected outfit shape.  Do you wish to update the current shape?  (click No to create a new shape)"), _("Merge or New"), wxYES_NO | wxICON_QUESTION, owner);
				if (ret == wxYES) {
					ret = wxMessageBox(_("Update Vertex Positions?"), _("Vertex Position Update"), wxYES_NO | wxICON_QUESTION, owner);
					if (ret == wxYES)
						workNif.SetVertsForShape(mergeShape, v);

					ret = wxMessageBox(_("Update Texture Coordinates?"), _("UV Update"), wxYES_NO | wxICON_QUESTION, owner);
					if (ret == wxYES)
						workNif.SetUvsForShape(mergeShape, uv);

					return 101;
				}
			}
			useShapeName = wxGetTextFromUser(_("Please specify a name for the new shape"), _("New Shape Name"), useShapeName, owner);
			if (useShapeName == "")
				return 100;
		}

		CreateNifShapeFromData(useShapeName, v, t, uv);
	}

	return 0;
}

int OutfitProject::ExportOBJ(const std::string& fileName, const std::vector<std::string>& shapes, Vector3 scale, Vector3 offset) {
	ObjFile obj;
	obj.SetScale(scale);
	obj.SetOffset(offset);

	for (auto &s : shapes) {
		std::vector<Triangle> tris;
		if (!workNif.GetTrisForShape(s, &tris))
			return 1;

		const std::vector<Vector3>* verts = workNif.GetRawVertsForShape(s);
		const std::vector<Vector2>* uvs = workNif.GetUvsForShape(s);

		obj.AddGroup(s, *verts, tris, *uvs);
	}

	if (obj.Save(fileName))
		return 2;

	return 0;
}

int OutfitProject::ImportFBX(const std::string& fileName, const std::string& shapeName, const std::string& mergeShape) {
	FBXWrangler fbxw;
	std::string nonRefBones;

	FBXImportDialog import(owner);
	if (import.ShowModal() != wxID_OK)
		return 1;

	bool result = fbxw.ImportScene(fileName, import.GetOptions());
	if (!result)
		return 2;

	if (!shapeName.empty())
		outfitName = shapeName;
	else if (outfitName.empty())
		outfitName = "New Outfit";

	std::vector<std::string>shapes;
	fbxw.GetShapeNames(shapes);
	for (auto &s : shapes) {
		FBXShape* shape = fbxw.GetShape(s);
		std::string useShapeName = s;

		if (!mergeShape.empty()) {
			int vertCount = workNif.GetVertCountForShape(mergeShape);
			if (vertCount == shape->verts.size()) {
				int ret = wxMessageBox(_("The vertex count of the selected .fbx file matches the currently selected outfit shape.  Do you wish to update the current shape?  (click No to create a new shape)"), _("Merge or New"), wxYES_NO | wxICON_QUESTION, owner);
				if (ret == wxYES) {
					ret = wxMessageBox(_("Update Vertex Positions?"), _("Vertex Position Update"), wxYES_NO | wxICON_QUESTION, owner);
					if (ret == wxYES)
						workNif.SetVertsForShape(mergeShape, shape->verts);

					ret = wxMessageBox(_("Update Texture Coordinates?"), _("UV Update"), wxYES_NO | wxICON_QUESTION, owner);
					if (ret == wxYES)
						workNif.SetUvsForShape(mergeShape, shape->uvs);

					ret = wxMessageBox(_("Update Animation Weighting?"), _("Animation Weight Update"), wxYES_NO | wxICON_QUESTION, owner);
					if (ret == wxYES)
						for (auto &bn : shape->boneNames)
							workAnim.SetWeights(mergeShape, bn, shape->boneSkin[bn].GetWeights());

					return 101;
				}
			}

			useShapeName = wxGetTextFromUser(_("Please specify a name for the new shape"), _("New Shape Name"), useShapeName, owner);
			if (useShapeName.empty())
				return 100;
		}

		CreateNifShapeFromData(s, shape->verts, shape->tris, shape->uvs, &shape->normals);

		int slot = 0;
		std::vector<int> boneIndices;
		for (auto &bn : shape->boneNames) {
			if (!AnimSkeleton::getInstance().RefBone(bn)) {
				// Not found in reference skeleton, use default values
				AnimBone& cstm = AnimSkeleton::getInstance().AddBone(bn, true);
				if (!cstm.isValidBone)
					nonRefBones += bn + "\n";

				AnimSkeleton::getInstance().RefBone(bn);
			}

			workAnim.shapeBones[useShapeName].push_back(bn);
			workAnim.shapeSkinning[useShapeName].boneNames[bn] = slot;
			workAnim.SetWeights(useShapeName, bn, shape->boneSkin[bn].GetWeights());
			boneIndices.push_back(slot++);
		}

		workNif.SetShapeBoneIDList(useShapeName, boneIndices);

		if (!nonRefBones.empty())
			wxLogMessage("Bones in shape '%s' not found in reference skeleton:\n%s", useShapeName, nonRefBones);
	}

	return 0;
}

int OutfitProject::ExportFBX(const std::string& fileName, const std::vector<std::string>& shapes) {
	FBXWrangler fbxw;
	fbxw.AddSkeleton(&AnimSkeleton::getInstance().refSkeletonNif);

	for (auto &s : shapes) {
		fbxw.AddNif(&workNif, s);
		fbxw.AddSkinning(&workAnim, s);
	}

	return fbxw.ExportScene(fileName);
}


void OutfitProject::CheckNIFTarget(NifFile& nif) {
	bool match = false;

	switch (owner->targetGame) {
	case FO3:
	case FONV:
		match = (nif.GetHeader().GetVersion().User2() == 34);
		break;
	case SKYRIM:
		match = (nif.GetHeader().GetVersion().User2() == 83);
		break;
	case FO4:
		match = (nif.GetHeader().GetVersion().User2() == 130);
		break;
	case SKYRIMSE:
		match = (nif.GetHeader().GetVersion().User2() == 100);
		break;
	}

	if (!match) {
		if (owner->targetGame == SKYRIMSE && nif.GetHeader().GetVersion().User2() == 83) {
			if (!Config.Exists("OptimizeForSSE")) {
				int res = wxMessageBox(_("Would you like Skyrim NIFs to be optimized for SSE during this session?"), _("Target Game"), wxYES_NO | wxICON_INFORMATION, owner);
				if (res == wxYES)
					Config.SetDefaultValue("OptimizeForSSE", "true");
				else
					Config.SetDefaultValue("OptimizeForSSE", "false");
			}

			if (Config["OptimizeForSSE"] == "true")
				nif.OptimizeForSSE();
		}
		else {
			wxLogWarning("Version of NIF file '%s' doesn't match current target game. To use the meshes for the target game, export to OBJ/FBX and reload them again.", nif.GetFileName());
			wxMessageBox(wxString::Format(_("Version of NIF file '%s' doesn't match current target game. To use the meshes for the target game, export to OBJ/FBX and reload them again."),
				nif.GetFileName()), _("Version"), wxICON_WARNING, owner);
		}
	}
}
