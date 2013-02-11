/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
#include "cmExportInstallFileGenerator.h"

#include "cmExportSet.h"
#include "cmExportSetMap.h"
#include "cmGeneratedFileStream.h"
#include "cmGlobalGenerator.h"
#include "cmLocalGenerator.h"
#include "cmInstallExportGenerator.h"
#include "cmInstallTargetGenerator.h"
#include "cmTargetExport.h"

//----------------------------------------------------------------------------
cmExportInstallFileGenerator
::cmExportInstallFileGenerator(cmInstallExportGenerator* iegen):
  IEGen(iegen)
{
}

//----------------------------------------------------------------------------
std::string cmExportInstallFileGenerator::GetConfigImportFileGlob()
{
  std::string glob = this->FileBase;
  glob += "-*";
  glob += this->FileExt;
  return glob;
}

//----------------------------------------------------------------------------
bool cmExportInstallFileGenerator::GenerateMainFile(std::ostream& os)
{
  std::vector<cmTarget*> allTargets;
  {
  std::string expectedTargets;
  std::string sep;
  for(std::vector<cmTargetExport*>::const_iterator
        tei = this->IEGen->GetExportSet()->GetTargetExports()->begin();
      tei != this->IEGen->GetExportSet()->GetTargetExports()->end(); ++tei)
    {
    expectedTargets += sep + this->Namespace + (*tei)->Target->GetName();
    sep = " ";
    cmTargetExport const* te = *tei;
    if(this->ExportedTargets.insert(te->Target).second)
      {
      allTargets.push_back(te->Target);
      }
    else
      {
      cmOStringStream e;
      e << "install(EXPORT \""
        << this->IEGen->GetExportSet()->GetName()
        << "\" ...) " << "includes target \"" << te->Target->GetName()
        << "\" more than once in the export set.";
      cmSystemTools::Error(e.str().c_str());
      return false;
      }
    }

  this->GenerateExpectedTargetsCode(os, expectedTargets);
  }

  std::vector<std::string> missingTargets;

  // Create all the imported targets.
  for(std::vector<cmTarget*>::const_iterator
        tei = allTargets.begin();
      tei != allTargets.end(); ++tei)
    {
    cmTarget* te = *tei;
    this->GenerateImportTargetCode(os, te);

    ImportPropertyMap properties;

    this->PopulateInterfaceProperty("INTERFACE_INCLUDE_DIRECTORIES",
                                  te,
                                  cmGeneratorExpression::InstallInterface,
                                  properties, missingTargets);
    this->PopulateInterfaceProperty("INTERFACE_COMPILE_DEFINITIONS",
                                  te,
                                  cmGeneratorExpression::InstallInterface,
                                  properties, missingTargets);
    this->PopulateInterfaceProperty("INTERFACE_POSITION_INDEPENDENT_CODE",
                                  te, properties);
    this->PopulateCompatibleInterfaceProperties(te, properties);

    this->GenerateInterfaceProperties(te, os, properties);
    }


  // Now load per-configuration properties for them.
  os << "# Load information for each installed configuration.\n"
     << "get_filename_component(_DIR \"${CMAKE_CURRENT_LIST_FILE}\" PATH)\n"
     << "file(GLOB CONFIG_FILES \"${_DIR}/"
     << this->GetConfigImportFileGlob() << "\")\n"
     << "foreach(f ${CONFIG_FILES})\n"
     << "  include(${f})\n"
     << "endforeach()\n"
     << "\n";

  this->GenerateImportedFileCheckLoop(os);

  // Generate an import file for each configuration.
  bool result = true;
  for(std::vector<std::string>::const_iterator
        ci = this->Configurations.begin();
      ci != this->Configurations.end(); ++ci)
    {
    if(!this->GenerateImportFileConfig(ci->c_str(), missingTargets))
      {
      result = false;
      }
    }

  this->GenerateMissingTargetsCheckCode(os, missingTargets);

  return result;
}

//----------------------------------------------------------------------------
bool
cmExportInstallFileGenerator::GenerateImportFileConfig(const char* config,
                                    std::vector<std::string> &missingTargets)
{
  // Skip configurations not enabled for this export.
  if(!this->IEGen->InstallsForConfig(config))
    {
    return true;
    }

  // Construct the name of the file to generate.
  std::string fileName = this->FileDir;
  fileName += "/";
  fileName += this->FileBase;
  fileName += "-";
  if(config && *config)
    {
    fileName += cmSystemTools::LowerCase(config);
    }
  else
    {
    fileName += "noconfig";
    }
  fileName += this->FileExt;

  // Open the output file to generate it.
  cmGeneratedFileStream exportFileStream(fileName.c_str(), true);
  if(!exportFileStream)
    {
    std::string se = cmSystemTools::GetLastSystemError();
    cmOStringStream e;
    e << "cannot write to file \"" << fileName.c_str()
      << "\": " << se;
    cmSystemTools::Error(e.str().c_str());
    return false;
    }
  std::ostream& os = exportFileStream;

  // Start with the import file header.
  this->GenerateImportHeaderCode(os, config);

  // Generate the per-config target information.
  this->GenerateImportConfig(os, config, missingTargets);

  // End with the import file footer.
  this->GenerateImportFooterCode(os);

  // Record this per-config import file.
  this->ConfigImportFiles[config] = fileName;

  return true;
}

//----------------------------------------------------------------------------
void
cmExportInstallFileGenerator
::GenerateImportTargetsConfig(std::ostream& os,
                              const char* config, std::string const& suffix,
                              std::vector<std::string> &missingTargets)
{
  // Add code to compute the installation prefix relative to the
  // import file location.
  const char* installDest = this->IEGen->GetDestination();
  if(!cmSystemTools::FileIsFullPath(installDest))
    {
    std::string dest = installDest;
    os << "# Compute the installation prefix relative to this file.\n"
       << "get_filename_component(_IMPORT_PREFIX "
       << "\"${CMAKE_CURRENT_LIST_FILE}\" PATH)\n";
    while(!dest.empty())
      {
      os <<
        "get_filename_component(_IMPORT_PREFIX \"${_IMPORT_PREFIX}\" PATH)\n";
      dest = cmSystemTools::GetFilenamePath(dest);
      }
    os << "\n";

    // Import location properties may reference this variable.
    this->ImportPrefix = "${_IMPORT_PREFIX}/";
    }

  // Add each target in the set to the export.
  for(std::vector<cmTargetExport*>::const_iterator
        tei = this->IEGen->GetExportSet()->GetTargetExports()->begin();
      tei != this->IEGen->GetExportSet()->GetTargetExports()->end(); ++tei)
    {
    // Collect import properties for this target.
    cmTargetExport const* te = *tei;
    ImportPropertyMap properties;
    std::set<std::string> importedLocations;
    this->SetImportLocationProperty(config, suffix, te->ArchiveGenerator,
                                    properties, importedLocations);
    this->SetImportLocationProperty(config, suffix, te->LibraryGenerator,
                                    properties, importedLocations);
    this->SetImportLocationProperty(config, suffix,
                                    te->RuntimeGenerator, properties,
                                    importedLocations);
    this->SetImportLocationProperty(config, suffix, te->FrameworkGenerator,
                                    properties, importedLocations);
    this->SetImportLocationProperty(config, suffix, te->BundleGenerator,
                                    properties, importedLocations);

    // If any file location was set for the target add it to the
    // import file.
    if(!properties.empty())
      {
      // Get the rest of the target details.
      this->SetImportDetailProperties(config, suffix,
                                      te->Target, properties, missingTargets);

      this->SetImportLinkInterface(config, suffix,
                                   cmGeneratorExpression::InstallInterface,
                                   te->Target, properties, missingTargets);

      // TOOD: PUBLIC_HEADER_LOCATION
      // This should wait until the build feature propagation stuff
      // is done.  Then this can be a propagated include directory.
      // this->GenerateImportProperty(config, te->HeaderGenerator,
      //                              properties);

      // Generate code in the export file.
      this->GenerateImportPropertyCode(os, config, te->Target, properties);
      this->GenerateImportedFileChecksCode(os, te->Target, properties,
                                           importedLocations);
      }
    }

  // Cleanup the import prefix variable.
  if(!this->ImportPrefix.empty())
    {
    os << "# Cleanup temporary variables.\n"
       << "set(_IMPORT_PREFIX)\n"
       << "\n";
    }
}

//----------------------------------------------------------------------------
void
cmExportInstallFileGenerator
::SetImportLocationProperty(const char* config, std::string const& suffix,
                            cmInstallTargetGenerator* itgen,
                            ImportPropertyMap& properties,
                            std::set<std::string>& importedLocations
                           )
{
  // Skip rules that do not match this configuration.
  if(!(itgen && itgen->InstallsForConfig(config)))
    {
    return;
    }

  // Get the target to be installed.
  cmTarget* target = itgen->GetTarget();

  // Construct the installed location of the target.
  std::string dest = itgen->GetDestination();
  std::string value;
  if(!cmSystemTools::FileIsFullPath(dest.c_str()))
    {
    // The target is installed relative to the installation prefix.
    if(this->ImportPrefix.empty())
      {
      this->ComplainAboutImportPrefix(itgen);
      }
    value = this->ImportPrefix;
    }
  value += dest;
  value += "/";

  if(itgen->IsImportLibrary())
    {
    // Construct the property name.
    std::string prop = "IMPORTED_IMPLIB";
    prop += suffix;

    // Append the installed file name.
    value += itgen->GetInstallFilename(target, config,
                                       cmInstallTargetGenerator::NameImplib);

    // Store the property.
    properties[prop] = value;
    importedLocations.insert(prop);
    }
  else
    {
    // Construct the property name.
    std::string prop = "IMPORTED_LOCATION";
    prop += suffix;

    // Append the installed file name.
    if(target->IsFrameworkOnApple())
      {
      value += itgen->GetInstallFilename(target, config);
      value += ".framework/";
      value += itgen->GetInstallFilename(target, config);
      }
    else if(target->IsCFBundleOnApple())
      {
      const char *ext = target->GetProperty("BUNDLE_EXTENSION");
      if (!ext)
        {
        ext = "bundle";
        }

      value += itgen->GetInstallFilename(target, config);
      value += ".";
      value += ext;
      value += "/";
      value += itgen->GetInstallFilename(target, config);
      }
    else if(target->IsAppBundleOnApple())
      {
      value += itgen->GetInstallFilename(target, config);
      value += ".app/Contents/MacOS/";
      value += itgen->GetInstallFilename(target, config);
      }
    else
      {
      value += itgen->GetInstallFilename(target, config,
                                         cmInstallTargetGenerator::NameReal);
      }

    // Store the property.
    properties[prop] = value;
    importedLocations.insert(prop);
    }
}

//----------------------------------------------------------------------------
void
cmExportInstallFileGenerator::HandleMissingTarget(
  std::string& link_libs, std::vector<std::string>& missingTargets,
  cmMakefile* mf, cmTarget* depender, cmTarget* dependee)
{
  std::string name = dependee->GetName();
  std::vector<std::string> namespaces = this->FindNamespaces(mf, name);
  int targetOccurrences = (int)namespaces.size();
  if (targetOccurrences == 1)
    {
    std::string missingTarget = namespaces[0];
    missingTarget += name;
    link_libs += missingTarget;
    missingTargets.push_back(missingTarget);
    }
  else
    {
    // We are not appending, so all exported targets should be
    // known here.  This is probably user-error.
    this->ComplainAboutMissingTarget(depender, dependee, targetOccurrences);
    }
}

//----------------------------------------------------------------------------
std::vector<std::string>
cmExportInstallFileGenerator
::FindNamespaces(cmMakefile* mf, const std::string& name)
{
  std::vector<std::string> namespaces;
  cmGlobalGenerator* gg = mf->GetLocalGenerator()->GetGlobalGenerator();
  const cmExportSetMap& exportSets = gg->GetExportSets();

  for(cmExportSetMap::const_iterator expIt = exportSets.begin();
      expIt != exportSets.end();
      ++expIt)
    {
    const cmExportSet* exportSet = expIt->second;
    std::vector<cmTargetExport*> const* targets =
                                                 exportSet->GetTargetExports();

    bool containsTarget = false;
    for(unsigned int i=0; i<targets->size(); i++)
      {
      if (name == (*targets)[i]->Target->GetName())
        {
        containsTarget = true;
        break;
        }
      }

    if (containsTarget)
      {
      std::vector<cmInstallExportGenerator const*> const* installs =
                                                 exportSet->GetInstallations();
      for(unsigned int i=0; i<installs->size(); i++)
        {
        namespaces.push_back((*installs)[i]->GetNamespace());
        }
      }
    }

  return namespaces;
}


//----------------------------------------------------------------------------
void
cmExportInstallFileGenerator
::ComplainAboutImportPrefix(cmInstallTargetGenerator* itgen)
{
  const char* installDest = this->IEGen->GetDestination();
  cmOStringStream e;
  e << "install(EXPORT \""
    << this->IEGen->GetExportSet()->GetName()
    << "\") given absolute "
    << "DESTINATION \"" << installDest << "\" but the export "
    << "references an installation of target \""
    << itgen->GetTarget()->GetName() << "\" which has relative "
    << "DESTINATION \"" << itgen->GetDestination() << "\".";
  cmSystemTools::Error(e.str().c_str());
}

//----------------------------------------------------------------------------
void
cmExportInstallFileGenerator
::ComplainAboutMissingTarget(cmTarget* depender,
                             cmTarget* dependee,
                             int occurrences)
{
  cmOStringStream e;
  e << "install(EXPORT \""
    << this->IEGen->GetExportSet()->GetName()
    << "\" ...) "
    << "includes target \"" << depender->GetName()
    << "\" which requires target \"" << dependee->GetName() << "\" ";
  if (occurrences == 0)
    {
    e << "that is not in the export set.";
    }
  else
    {
    e << "that is not in this export set, but " << occurrences
    << " times in others.";
    }
  cmSystemTools::Error(e.str().c_str());
}
