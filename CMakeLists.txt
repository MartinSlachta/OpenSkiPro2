cmake_minimum_required(VERSION 3.21)

project(OpenSkiPro2 VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

option(OSP_BUILD_TESTS "Build unit tests" ON)
option(OSP_BUILD_APP "Build the application" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Sql SerialPort Network PrintSupport HttpServer WebSockets LinguistTools)

qt_standard_project_setup()

# Core library (no GUI widgets) - shared by the application and the tests
add_library(osp_core STATIC
    src/core/Types.h
    src/core/TimeUtil.h            src/core/TimeUtil.cpp
    src/core/Competitor.h          src/core/Competitor.cpp
    src/core/EventParams.h         src/core/EventParams.cpp
    src/core/RunData.h
    src/core/Category.h            src/core/Category.cpp
    src/core/OutputGroup.h         src/core/OutputGroup.cpp
    src/core/FieldFormat.h         src/core/FieldFormat.cpp
    src/core/Event.h               src/core/Event.cpp
    src/core/Ranking.h             src/core/Ranking.cpp
    src/core/StartListBuilder.h    src/core/StartListBuilder.cpp
    src/core/FisList.h             src/core/FisList.cpp
    src/core/FisCompetitors.h      src/core/FisCompetitors.cpp
    src/core/BibDraw.h             src/core/BibDraw.cpp
    src/core/FisPenalty.h          src/core/FisPenalty.cpp
    src/core/FisXml.h              src/core/FisXml.cpp
    src/db/EventDb.h               src/db/EventDb.cpp
    src/db/SkiProImporter.h        src/db/SkiProImporter.cpp
    src/db/SkiAlpImporter.h        src/db/SkiAlpImporter.cpp
    src/db/CsvImporter.h           src/db/CsvImporter.cpp
    src/db/FisListDb.h             src/db/FisListDb.cpp
    src/timing/Cp540Protocol.h     src/timing/Cp540Protocol.cpp
    src/timing/TimingDevice.h      src/timing/TimingDevice.cpp
    src/timing/Cp540Device.h       src/timing/Cp540Device.cpp
    src/timing/Cp540Simulator.h    src/timing/Cp540Simulator.cpp
    src/timing/TimingEngine.h      src/timing/TimingEngine.cpp
    src/report/ReportFormat.h      src/report/ReportFormat.cpp
    src/report/ReportBuilder.h     src/report/ReportBuilder.cpp
    src/report/ReportRenderer.h    src/report/ReportRenderer.cpp
    src/report/RefereeReport.h     src/report/RefereeReport.cpp
    src/app/AppSettings.h          src/app/AppSettings.cpp
    src/app/SoundPlayer.h          src/app/SoundPlayer.cpp
    src/api/ApiJson.h              src/api/ApiJson.cpp
    src/api/LiveState.h            src/api/LiveState.cpp
    src/api/ApiServer.h            src/api/ApiServer.cpp
    src/api/ApiTestPage.h          src/api/ApiTestPage.cpp
)
target_include_directories(osp_core PUBLIC src)
# OpenAPI description of the API, served by the API itself (docs/api.md),
# and Swagger UI (swagger-ui-dist, Apache-2.0, src/api/swagger/README.txt) at /docs
qt_add_resources(osp_core "api_resources" PREFIX "/api" BASE src/api FILES
    src/api/openapi.yaml src/api/swagger/swagger-ui-bundle.js src/api/swagger/swagger-ui.css
    src/api/swagger/LICENSE src/api/swagger/NOTICE)
target_link_libraries(osp_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Sql Qt6::SerialPort Qt6::Network Qt6::PrintSupport Qt6::HttpServer Qt6::WebSockets)
target_link_libraries(osp_core PRIVATE Qt6::CorePrivate)
if(WIN32)
    target_link_libraries(osp_core PUBLIC winmm)
endif()
target_compile_definitions(osp_core PUBLIC
    OSP_VERSION="${PROJECT_VERSION}")

if(OSP_BUILD_APP)
qt_add_executable(OpenSkiPro2 WIN32
    src/main.cpp
    src/ui/MainWindow.h                       src/ui/MainWindow.cpp
    src/ui/NavPanel.h                         src/ui/NavPanel.cpp
    src/ui/HeaderBanner.h                     src/ui/HeaderBanner.cpp
    src/ui/CompetitorsPage.h                  src/ui/CompetitorsPage.cpp
    src/ui/RunsPage.h                         src/ui/RunsPage.cpp
    src/ui/TimingWindow.h                     src/ui/TimingWindow.cpp
    src/ui/UiHelpers.h                        src/ui/UiHelpers.cpp
    src/ui/Snapshots.h                        src/ui/Snapshots.cpp
    src/ui/FisUi.h                            src/ui/FisUi.cpp
    src/ui/FisListPage.h                      src/ui/FisListPage.cpp
    src/ui/FisCompetitorsWindow.h             src/ui/FisCompetitorsWindow.cpp
    src/ui/dialogs/SelectEventDialog.h        src/ui/dialogs/SelectEventDialog.cpp
    src/ui/dialogs/GeneralParametersDialog.h  src/ui/dialogs/GeneralParametersDialog.cpp
    src/ui/dialogs/EventTypeDialog.h          src/ui/dialogs/EventTypeDialog.cpp
    src/ui/dialogs/PrintingParametersDialog.h src/ui/dialogs/PrintingParametersDialog.cpp
    src/ui/dialogs/LogoPositionsDialog.h      src/ui/dialogs/LogoPositionsDialog.cpp
    src/ui/dialogs/HeaderDialog.h             src/ui/dialogs/HeaderDialog.cpp
    src/ui/dialogs/CompetitorDialog.h         src/ui/dialogs/CompetitorDialog.cpp
    src/ui/dialogs/CompetitorFieldsDialog.h   src/ui/dialogs/CompetitorFieldsDialog.cpp
    src/ui/dialogs/CategoriesDialog.h         src/ui/dialogs/CategoriesDialog.cpp
    src/ui/dialogs/AssignBibsDialog.h         src/ui/dialogs/AssignBibsDialog.cpp
    src/ui/dialogs/DoubleDrawDialog.h         src/ui/dialogs/DoubleDrawDialog.cpp
    src/ui/dialogs/CsvImportDialog.h          src/ui/dialogs/CsvImportDialog.cpp
    src/ui/dialogs/StartListDialog.h          src/ui/dialogs/StartListDialog.cpp
    src/ui/dialogs/ModifyDataDialog.h         src/ui/dialogs/ModifyDataDialog.cpp
    src/ui/dialogs/UnprocessedDialog.h        src/ui/dialogs/UnprocessedDialog.cpp
    src/ui/dialogs/TimingDeviceDialog.h       src/ui/dialogs/TimingDeviceDialog.cpp
    src/ui/dialogs/TimingOptionsDialog.h      src/ui/dialogs/TimingOptionsDialog.cpp
    src/ui/dialogs/ImpulseCommentDialog.h     src/ui/dialogs/ImpulseCommentDialog.cpp
    src/ui/dialogs/OutputGroupsDialog.h       src/ui/dialogs/OutputGroupsDialog.cpp
    src/ui/dialogs/FormatFieldDialog.h        src/ui/dialogs/FormatFieldDialog.cpp
    src/ui/dialogs/FoldersDialog.h            src/ui/dialogs/FoldersDialog.cpp
    src/ui/dialogs/ApiSettingsDialog.h        src/ui/dialogs/ApiSettingsDialog.cpp
    src/ui/dialogs/ReportDialog.h             src/ui/dialogs/ReportDialog.cpp
    src/ui/dialogs/PreviewDialog.h            src/ui/dialogs/PreviewDialog.cpp
    src/ui/dialogs/AboutDialog.h              src/ui/dialogs/AboutDialog.cpp
    src/ui/dialogs/FisImportDialog.h          src/ui/dialogs/FisImportDialog.cpp
    src/ui/dialogs/FisUpdateDialog.h          src/ui/dialogs/FisUpdateDialog.cpp
    src/ui/dialogs/FisPenaltyDialog.h         src/ui/dialogs/FisPenaltyDialog.cpp
    src/ui/dialogs/RefereeReportDialog.h      src/ui/dialogs/RefereeReportDialog.cpp
    resources/resources.qrc
    resources/app.rc
)
target_link_libraries(OpenSkiPro2 PRIVATE osp_core Qt6::Widgets Qt6::PrintSupport)

# Translations: English is the source language. Further languages are added
# by listing translations/openskipro2_<lang>.ts here.
qt_add_translations(OpenSkiPro2
    SOURCE_TARGETS OpenSkiPro2 osp_core
    TS_FILES translations/openskipro2_en.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete)
endif()

# osp-apicheck: black box of the API (records a timed run, checks it against REST)
add_library(osp_apicheck STATIC tools/apicheck/ApiCheck.h tools/apicheck/ApiCheck.cpp)
target_include_directories(osp_apicheck PUBLIC tools/apicheck)
target_link_libraries(osp_apicheck PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets)
add_executable(osp-apicheck tools/apicheck/main.cpp)
target_link_libraries(osp-apicheck PRIVATE osp_apicheck)

# the tests are not part of the public repository
if(OSP_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
