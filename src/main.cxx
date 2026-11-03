#include <fmt/ostream.h>
#include <spdlog/spdlog.h>

#include <boost/program_options.hpp>
#include <boost/program_options/errors.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "adapters/SmpsTextAdapter/SmpsTextAdapter.hxx"
#include "utils/VgmDecompressor.hxx"
#include "utils/getTerminalWidth.hxx"
#include "version.hxx"
#include "vgm_parser/VgmParser.hxx"

namespace vgmc {
template <typename OutputAdapter, typename... AdapterArgs>
static void runConversion(std::span<const uint8_t> data, std::ostream& out,
                          VgmParserSettings parserSettings,
                          AdapterArgs&&... adapterArgs) {
  OutputAdapter adapter{std::forward<AdapterArgs>(adapterArgs)...};
  VgmParser<OutputAdapter> parser{std::move(parserSettings)};
  parser.parse(data, adapter);
  // TODO: before write, show the GUI with additional settings/fine-tuning
  adapter.writeTo(out);
}
}  // namespace vgmc

void commandLineParse(int argc, char** argv, std::string& inputFilePath,
                      std::string& outputPath,
                      vgmc::VgmParserSettings& parserSettings,
                      vgmc::SmpsAdapterBaseSettings& adapterSettings) {
  namespace po = boost::program_options;

  if (argc == 1) {
    fmt::println(std::cerr,
                 "Input file path not specified! Try to run \"{} --help\" for "
                 "more information",
                 argv[0]);
    exit(EXIT_SUCCESS);
  }

  // Declare the supported options.
  po::options_description desc{
      "vgms2msps is a tool that converts VGM files to SMPS format "
      "(Sample Music Playback System), used by various games.\n"
      "Allowed options",
      getTerminalWidth()};
  std::string smpsDriverVersion{"S3K"};

  // clang-format off
  desc.add_options()
    ("help", "Print help message and exit")
    ("version", "Print project version and exit")
    ("input,i",  po::value(&inputFilePath), "Input file path (vgm/vgz)")
    ("output,o",  po::value(&outputPath)->default_value(outputPath), "Output path. If not specified stdout will be used")
    ("length,L", po::value<uint32_t>(&adapterSettings.maxLength), "Reduce output length in frames")
    ("fm-mask", po::value<std::string>()->default_value(adapterSettings.ym2612.channelsEnabled.to_string()), "Mask for enable YM2612 (FM) channels")
    ("psg-mask", po::value<std::string>()->default_value(adapterSettings.sn76489.channelsEnabled.to_string()), "Mask for enable SN76489 (PSG) channels")
    ("disable-dac", "Disable DAC in output")
    ("smps-driver-version", po::value(&smpsDriverVersion)->default_value(smpsDriverVersion), "SMPS Driver version (S1, S2 or S3K)")
  ;
  // clang-format on

  po::variables_map vm{};
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.contains("help")) {
    desc.print(std::cout);
    exit(EXIT_SUCCESS);
  }

  if (vm.contains("version")) {
    fmt::println("vgm2smps version: {}", VGMC_VERSION);
#ifdef VGMC_GIT_COMMIT_HASH
    fmt::println("commit: {}", VGMC_GIT_COMMIT_HASH);
#endif
#ifdef VGMC_GIT_COMMIT_DATE
    fmt::println("commit date: {}", VGMC_GIT_COMMIT_DATE);
#endif
    fmt::println("");
    fmt::println("vgm2smps created by Ivan Platonov (aka N-BAH).");
    exit(EXIT_SUCCESS);
  }

  if (not vm.contains("input")) {
    fmt::println(std::cerr, "Input file path not specified!");
    exit(EXIT_FAILURE);
  }

  if (not std::filesystem::exists(inputFilePath)) {
    fmt::println(std::cerr, "Specified not existing file:{}. Exit.",
                 inputFilePath);
    exit(EXIT_FAILURE);
  }

  if (vm.contains("fm-mask")) {
    adapterSettings.ym2612.channelsEnabled =
        decltype(adapterSettings.ym2612.channelsEnabled){
            vm["fm-mask"].as<std::string>()};
  }

  if (vm.contains("psg-mask")) {
    adapterSettings.sn76489.channelsEnabled =
        decltype(adapterSettings.sn76489.channelsEnabled){
            vm["psg-mask"].as<std::string>()};
  }

  if (vm.contains("smps-driver-version")) {
    if (smpsDriverVersion == "S1") {
      adapterSettings.driverVersion = vgmc::SmpsDriverVersion::S1;
    } else if (smpsDriverVersion == "S2") {
      adapterSettings.driverVersion = vgmc::SmpsDriverVersion::S2;
    } else if (smpsDriverVersion == "S3K") {
      adapterSettings.driverVersion = vgmc::SmpsDriverVersion::S3K;
    } else {
      fmt::println(std::cerr, "Unknown SMPS Driver version specified:{}. Exit.",
                   smpsDriverVersion);
      exit(EXIT_FAILURE);
    }
  }

  if (vm.contains("disable-dac")) {
    adapterSettings.ym2612.dacEnabled = false;
  }
}

auto main(int argc, char** argv) -> int {
  constexpr std::string_view OUTPUT_PATH_STDOUT{"--"};

  try {
    vgmc::VgmParserSettings parserSettings{};
    vgmc::SmpsAdapterBaseSettings adapterSettings{};
    std::string inputFilePath;
    std::string outputPath{OUTPUT_PATH_STDOUT};

    try {
      commandLineParse(argc, argv, inputFilePath, outputPath, parserSettings,
                       adapterSettings);
    } catch (const boost::program_options::error& e) {
      SPDLOG_ERROR("Command line: {}", e.what());
      return EXIT_FAILURE;
    }

    SPDLOG_INFO("Process VGM file: {}", inputFilePath);
    const std::vector<uint8_t> vgmData =
        vgmc::decompressVgmFileToRam(inputFilePath);

    std::ostream* out{};
    std::ofstream outFile;

    if (outputPath != OUTPUT_PATH_STDOUT) {
      outFile.open(outputPath, std::ios::trunc);
      out = &outFile;
    } else {
      out = &std::cout;
    }

    // TODO: Design settings system across adapters and mixins
    vgmc::runConversion<vgmc::SmpsTextAdapter>(vgmData, *out, parserSettings,
                                               adapterSettings);
    SPDLOG_INFO("Done!");
  } catch (const std::exception& e) {
    SPDLOG_CRITICAL("Unhandled exception: {}", e.what());
    return EXIT_FAILURE;
  } catch (...) {
    SPDLOG_CRITICAL("Unhandled unknown exception!");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
