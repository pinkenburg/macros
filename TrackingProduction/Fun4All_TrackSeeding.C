/*
 * This macro shows a working example of running TrackSeeding over the cluster DST
 * This has track residuals as default output but has KFParticle set up with a togglable flag
 * with the default set up for K Short reconstruction
 */

#include <fun4all/Fun4AllUtils.h>
#include <G4_ActsGeom.C>
#include <G4_Global.C>
#include <G4_Magnet.C>
#include <GlobalVariables.C>
#include <QA.C>
#include <Trkr_Clustering.C>
#include <Trkr_Reco.C>
#include <Trkr_RecoInit.C>
#include <Trkr_TpcReadoutInit.C>

#include <ffamodules/CDBInterface.h>

#include <fun4all/Fun4AllDstInputManager.h>
#include <fun4all/Fun4AllDstOutputManager.h>
#include <fun4all/Fun4AllInputManager.h>
#include <fun4all/Fun4AllOutputManager.h>
#include <fun4all/Fun4AllRunNodeInputManager.h>
#include <fun4all/Fun4AllServer.h>

#include <phool/recoConsts.h>

#include <cdbobjects/CDBTTree.h>

#include <tpccalib/PHTpcResiduals.h>

#include <trackingqa/SiliconSeedsQA.h>
#include <trackingqa/TpcSeedsQA.h>
#include <trackingqa/TpcSiliconQA.h>

#include <trackingdiagnostics/TrackResiduals.h>
#include <trackingdiagnostics/TrkrNtuplizer.h>

#include <kfparticle_sphenix/KFParticle_sPHENIX.h>

#include <stdio.h>

R__LOAD_LIBRARY(libkfparticle_sphenix.so)

R__LOAD_LIBRARY(libfun4all.so)
R__LOAD_LIBRARY(libffamodules.so)
R__LOAD_LIBRARY(libphool.so)
R__LOAD_LIBRARY(libcdbobjects.so)
R__LOAD_LIBRARY(libTrackingDiagnostics.so)
R__LOAD_LIBRARY(libtrackingqa.so)
void Fun4All_TrackSeeding(
    const int nEvents = 5,
    const std::string clusterfilename = "DST_TRKR_CLUSTER_run2pp_ana494_2024p021_v001-00053877-00000.root",
    const std::string dir = "/sphenix/lustre01/sphnxpro/production/run2pp/physics/ana494_2024p021_v001/DST_TRKR_CLUSTER/run_00053800_00053900/dst/",
    const std::string outfilename = "clusters_seeds",
    const bool convertSeeds = false,
    const bool doKFParticle = false)
{
  //std::string inputseedRawHitFile = dir + seedfilename;
  std::string inputclusterRawHitFile = dir + clusterfilename;

  G4TRACKING::convert_seeds_to_svtxtracks = convertSeeds;
  std::cout << "Converting to seeds : " << G4TRACKING::convert_seeds_to_svtxtracks << std::endl;
  std::pair<int, int>
      runseg = Fun4AllUtils::GetRunSegment(clusterfilename);
  int runnumber = runseg.first;
  int segment = runseg.second;

  auto rc = recoConsts::instance();
  rc->set_IntFlag("RUNNUMBER", runnumber);

  Enable::CDB = true;
  rc->set_StringFlag("CDB_GLOBALTAG", "newcdbtag");
  rc->set_uint64Flag("TIMESTAMP", runnumber);
  std::string geofile = CDBInterface::instance()->getUrl("Tracking_Geometry");

  TpcReadoutInit(runnumber);
 // these lines show how to override the drift velocity and time offset values set in TpcReadoutInit
  // G4TPC::tpc_drift_velocity_reco = 0.0073844; // cm/ns
  // TpcClusterZCrossingCorrection::_vdrift = G4TPC::tpc_drift_velocity_reco;
  // G4TPC::tpc_tzero_reco = -5*50;  // ns
  std::cout << " run: " << runnumber
            << " samples: " << TRACKING::reco_tpc_maxtime_sample
            << " pre: " << TRACKING::reco_tpc_time_presample
            << " vdrift: " << G4TPC::tpc_drift_velocity_reco
            << std::endl;

  string outDir = "myKShortReco/";
  string outputFileName = "outputKFParticle_" + to_string(runnumber) + "_" + to_string(segment) + ".root";
  string outputRecoDir = outDir + "inReconstruction/";
  string outputRecoFile = outputRecoDir + outputFileName;

  if(doKFParticle){
    string makeMainDirectory = "mkdir -p " + outDir;
    system(makeMainDirectory.c_str());
    string makeDirectory = "mkdir -p " + outputRecoDir;
    system(makeDirectory.c_str());
  }

  // distortion calibration mode
  /*
   * set to true to enable residuals in the TPC with
   * TPC clusters not participating to the ACTS track fit
   */
  G4TRACKING::SC_CALIBMODE = false;
  TRACKING::pp_mode = true;
  
  Enable::MVTX_APPLYMISALIGNMENT = true;
  ACTSGEOM::mvtx_applymisalignment = Enable::MVTX_APPLYMISALIGNMENT;
  
  TString outfile = outfilename + "_" + runnumber + "-" + segment + ".root";
  std::string theOutfile = outfile.Data();
  auto se = Fun4AllServer::instance();
  se->Verbosity(1);


  Fun4AllRunNodeInputManager *ingeo = new Fun4AllRunNodeInputManager("GeoIn");
  ingeo->AddFile(geofile);
  se->registerInputManager(ingeo);

  G4TPC::ENABLE_MODULE_EDGE_CORRECTIONS = true;

  // to turn on the default static corrections, enable the two lines below
  G4TPC::ENABLE_STATIC_CORRECTIONS = true;
  G4TPC::USE_PHI_AS_RAD_STATIC_CORRECTIONS = false;

  //to turn on the average corrections, enable the three lines below
  //note: these are designed to be used only if static corrections are also applied
  G4TPC::ENABLE_AVERAGE_CORRECTIONS = true;
  G4TPC::USE_PHI_AS_RAD_AVERAGE_CORRECTIONS = false;
   // to use a custom file instead of the database file:
  G4TPC::average_correction_filename = CDBInterface::instance()->getUrl("TPC_LAMINATION_FIT_CORRECTION");
   
  G4MAGNET::magfield_rescale = 1;
  TrackingInit();

  auto hitsinclus = new Fun4AllDstInputManager("ClusterInputManager");
  hitsinclus->fileopen(inputclusterRawHitFile);
  se->registerInputManager(hitsinclus);

  // reject laser events if G4TPC::REJECT_LASER_EVENTS is true
  Reject_Laser_Events();

  /*
   * Begin Track Seeding
   */

  /*
   * Silicon Seeding
   */

  auto silicon_Seeding = new PHActsSiliconSeeding;
  silicon_Seeding->Verbosity(0);
  silicon_Seeding->setStrobeRange(-5,5);
  // these get us to about 83% INTT > 1
  silicon_Seeding->setinttRPhiSearchWindow(0.2);
  silicon_Seeding->setinttZSearchWindow(1.0);
  silicon_Seeding->seedAnalysis(false);
  se->registerSubsystem(silicon_Seeding);

  auto merger = new PHSiliconSeedMerger;
  merger->Verbosity(0);
  se->registerSubsystem(merger);

  /*
   * Tpc Seeding
   */
  auto seeder = new PHCASeeding("PHCASeeding");
  double fieldstrength = std::numeric_limits<double>::quiet_NaN();  // set by isConstantField if constant
  bool ConstField = isConstantField(G4MAGNET::magfield_tracking, fieldstrength);
  if (ConstField)
  {
    seeder->useConstBField(true);
    seeder->constBField(fieldstrength);
  }
  else
  {
    seeder->set_field_dir(-1 * G4MAGNET::magfield_rescale);
    seeder->useConstBField(false);
    seeder->magFieldFile(G4MAGNET::magfield_tracking);  // to get charge sign right
  }
  seeder->Verbosity(0);
  seeder->SetLayerRange(7, 55);
  seeder->SetSearchWindow(2.,0.05); // z-width and phi-width, default in macro at 1.5 and 0.05
  seeder->SetClusAdd_delta_window(3.0,0.06); //  (0.5, 0.005) are default; sdzdr_cutoff, d2/dr2(phi)_cutoff
  //seeder->SetNClustersPerSeedRange(4,60); // default is 6, 6
  seeder->SetMinHitsPerCluster(0);
  seeder->SetMinClustersPerTrack(3);
  seeder->useFixedClusterError(true);
  seeder->set_pp_mode(true);
  seeder->reject_zsize1_clusters(true);
  se->registerSubsystem(seeder);

  // expand stubs in the TPC using simple kalman filter
  auto cprop = new PHSimpleKFProp("PHSimpleKFProp");
  cprop->set_field_dir(G4MAGNET::magfield_rescale);
  if (ConstField)
  {
    cprop->useConstBField(true);
    cprop->setConstBField(fieldstrength);
  }
  else
  {
    cprop->magFieldFile(G4MAGNET::magfield_tracking);
    cprop->set_field_dir(-1 * G4MAGNET::magfield_rescale);
  }
  cprop->useFixedClusterError(true);
  cprop->set_max_window(5.);
  cprop->Verbosity(0);
  cprop->set_pp_mode(true);
  cprop->set_max_seeds(5000);
  se->registerSubsystem(cprop);

  // Always apply preliminary distortion corrections to TPC clusters before silicon matching
  // and refit the trackseeds. Replace KFProp fits with the new fit parameters in the TPC seeds.
  auto prelim_distcorr = new PrelimDistortionCorrection;
  prelim_distcorr->set_pp_mode(true);
  prelim_distcorr->Verbosity(0);
  se->registerSubsystem(prelim_distcorr);

  /*
   * Track Matching between silicon and TPC
   */
  // The normal silicon association methods
  // Match the TPC track stubs from the CA seeder to silicon track stubs from PHSiliconTruthTrackSeeding
  auto silicon_match = new PHSiliconTpcTrackMatching;
  silicon_match->Verbosity(0);
  silicon_match->set_pp_mode(TRACKING::pp_mode);
  if(G4TPC::ENABLE_AVERAGE_CORRECTIONS)
  {
    // for general tracking
    // Eta/Phi window is determined by 3 sigma window
    // X/Y/Z window is determined by 4 sigma window
    silicon_match->window_deta.set_posQoverpT_maxabs({-0.014,0.0331,0.48});
    silicon_match->window_deta.set_negQoverpT_maxabs({-0.006,0.0235,0.52});
    silicon_match->set_deltaeta_min(0.03);
    silicon_match->window_dphi.set_QoverpT_range({-0.15,0,0}, {0.15,0,0});
    silicon_match->window_dx.set_QoverpT_maxabs({3.0,0,0});
    silicon_match->window_dy.set_QoverpT_maxabs({3.0,0,0});
    silicon_match->window_dz.set_posQoverpT_maxabs({1.138,0.3919,0.84});
    silicon_match->window_dz.set_negQoverpT_maxabs({0.719,0.6485,0.65});
    silicon_match->set_crossing_deltaz_max(30);
    silicon_match->set_crossing_deltaz_min(2);

    // for distortion correction using SI-TPOT fit and track pT>0.5
    if (G4TRACKING::SC_CALIBMODE)
    {
      silicon_match->window_deta.set_posQoverpT_maxabs({0.016,0.0060,1.13});
      silicon_match->window_deta.set_negQoverpT_maxabs({0.022,0.0022,1.44});
      silicon_match->set_deltaeta_min(0.03);
      silicon_match->window_dphi.set_QoverpT_range({-0.15,0,0}, {0.09,0,0});
      silicon_match->window_dx.set_QoverpT_maxabs({2.0,0,0});
      silicon_match->window_dy.set_QoverpT_maxabs({1.5,0,0});
      silicon_match->window_dz.set_posQoverpT_maxabs({1.213,0.0211,2.09});
      silicon_match->window_dz.set_negQoverpT_maxabs({1.307,0.0001,4.52});
      silicon_match->set_crossing_deltaz_min(1.2);
    }
  }
  se->registerSubsystem(silicon_match);

  // Match TPC track stubs from CA seeder to clusters in the micromegas layers
  auto mm_match = new PHMicromegasTpcTrackMatching;
  mm_match->Verbosity(0);
  mm_match->set_pp_mode(TRACKING::pp_mode);
  mm_match->set_rphi_search_window_lyr1(3.);
  mm_match->set_rphi_search_window_lyr2(15.0);
  mm_match->set_z_search_window_lyr1(30.0);
  mm_match->set_z_search_window_lyr2(3.);

  mm_match->set_min_tpc_layer(38);             // layer in TPC to start projection fit
  mm_match->set_test_windows_printout(false);  // used for tuning search windows only
  se->registerSubsystem(mm_match);

  /*
   * End Track Seeding
   */

  /*
   * Either converts seeds to tracks with a straight line/helix fit
   * or run the full Acts track kalman filter fit
   */
  if (G4TRACKING::convert_seeds_to_svtxtracks)
  {
    auto converter = new TrackSeedTrackMapConverter;
    // Default set to full SvtxTrackSeeds. Can be set to
    // SiliconTrackSeedContainer or TpcTrackSeedContainer
    converter->setTrackSeedName("SvtxTrackSeedContainer");
    converter->setFieldMap(G4MAGNET::magfield_tracking);
    converter->Verbosity(0);
    se->registerSubsystem(converter);
  }
  else
  {
    auto deltazcorr = new PHTpcDeltaZCorrection;
    deltazcorr->Verbosity(0);
    se->registerSubsystem(deltazcorr);

    // perform final track fit with ACTS
    auto actsFit = new PHActsTrkFitter;
    actsFit->Verbosity(0);
    actsFit->commissioning(G4TRACKING::use_alignment);
    // in calibration mode, fit only Silicons and Micromegas hits
    actsFit->fitSiliconMMs(G4TRACKING::SC_CALIBMODE);
    actsFit->setUseMicromegas(G4TRACKING::SC_USE_MICROMEGAS);
    actsFit->set_pp_mode(TRACKING::pp_mode);
    actsFit->set_use_clustermover(true);  // default is true for now
    actsFit->useActsEvaluator(false);
    actsFit->useOutlierFinder(false);
    actsFit->setFieldMap(G4MAGNET::magfield_tracking);
    se->registerSubsystem(actsFit);

    auto cleaner = new PHTrackCleaner();
    cleaner->Verbosity(0);
    cleaner->set_pp_mode(TRACKING::pp_mode);
    se->registerSubsystem(cleaner);

    if (G4TRACKING::SC_CALIBMODE)
    {
      /*
       * in calibration mode, calculate residuals between TPC and fitted tracks,
       * store in dedicated structure for distortion correction
       */
      auto residuals = new PHTpcResiduals;
      const TString tpc_residoutfile = theOutfile + "_PhTpcResiduals.root";
      residuals->setOutputfile(tpc_residoutfile.Data());
      residuals->setUseMicromegas(G4TRACKING::SC_USE_MICROMEGAS);

      // matches Tony's analysis
      residuals->setMinPt(0.2);

      // reconstructed distortion grid size (phi, r, z)
      residuals->setGridDimensions(36, 48, 80);
      se->registerSubsystem(residuals);
    }
  }

  auto finder = new PHSimpleVertexFinder;
  finder->Verbosity(0);
  
  //new cuts
  finder->setDcaCut(0.05);
  finder->setTrackPtCut(0.1);
  finder->setBeamLineCut(1);
  finder->setTrackQualityCut(300);
  finder->setNmvtxRequired(3);
  finder->setOutlierPairCut(0.10);
  
  se->registerSubsystem(finder);

  // Propagate track positions to the vertex position
  auto vtxProp = new PHActsVertexPropagator;
  vtxProp->Verbosity(0);
  vtxProp->fieldMap(G4MAGNET::magfield_tracking);
  se->registerSubsystem(vtxProp);
  
  //run KFParticle
  if(doKFParticle){
     Global_Reco();

  //KFParticle setup

  KFParticle_sPHENIX *kfparticle = new KFParticle_sPHENIX("myKShortReco");
  kfparticle->Verbosity(1);
  kfparticle->setDecayDescriptor("K_S0 -> pi^+ pi^-");

  //Basic node selection and configuration
  kfparticle->magFieldFile("FIELDMAP_TRACKING");
  kfparticle->getAllPVInfo(false);
  kfparticle->allowZeroMassTracks(true);
  kfparticle->useFakePrimaryVertex(false);
  kfparticle->getDetectorInfo(true);

  kfparticle->constrainToPrimaryVertex(true);
  kfparticle->setMotherIPchi2(FLT_MAX);
  kfparticle->setFlightDistancechi2(-1.);
  kfparticle->setMinDIRA(-1.1);
  kfparticle->setDecayLengthRange(0., FLT_MAX);
  kfparticle->setDecayTimeRange(-1*FLT_MAX, FLT_MAX);

  //Track parameters
  kfparticle->setMinMVTXhits(0);
  //kfparticle->setMinINTThits(0);
  kfparticle->setMinTPChits(20);
  kfparticle->setMinimumTrackPT(-1.);
  kfparticle->setMaximumTrackPTchi2(FLT_MAX);
  kfparticle->setMinimumTrackIPchi2(-1.);
  kfparticle->setMinimumTrackIP(-1.);
  //kfparticle->setMaximumTrackchi2nDOF(20.);
  kfparticle->setMaximumTrackchi2nDOF(300.);

  //Vertex parameters
  kfparticle->setMaximumVertexchi2nDOF(50);
  kfparticle->setMaximumDaughterDCA(1.);

  //Parent parameters
  kfparticle->setMotherPT(0);
  kfparticle->setMinimumMass(0.200);
  kfparticle->setMaximumMass(1.000);
  kfparticle->setMaximumMotherVertexVolume(0.1);

  kfparticle->setOutputName(outputRecoFile);

  se->registerSubsystem(kfparticle);
  }

  TString residoutfile = theOutfile + "_resid.root";
  std::string residstring(residoutfile.Data());

  auto resid = new TrackResiduals("TrackResiduals");
  resid->outfileName(residstring);
  resid->alignment(false);

  // adjust track map name
  if (G4TRACKING::SC_CALIBMODE && !G4TRACKING::convert_seeds_to_svtxtracks)
  {
    resid->trackmapName("SvtxSiliconMMTrackMap");
    if (G4TRACKING::SC_USE_MICROMEGAS)
    {
      resid->set_doMicromegasOnly(true);
    }
  }

  resid->clusterTree();
  resid->convertSeeds(G4TRACKING::convert_seeds_to_svtxtracks);
  resid->Verbosity(0);
  se->registerSubsystem(resid);

  if (Enable::QA)
  {
    se->registerSubsystem(new SiliconSeedsQA);
    se->registerSubsystem(new TpcSeedsQA);
    se->registerSubsystem(new TpcSiliconQA);
  }
  se->run(nEvents);
  se->End();
  se->PrintTimer();
  CDBInterface::instance()->Print();
  if (Enable::QA)
  {
    TString qaname = theOutfile + "_qa.root";
    std::string qaOutputFileName(qaname.Data());
    QAHistManagerDef::saveQARootFile(qaOutputFileName);
  }

  if(doKFParticle){
    ifstream file(outputRecoFile.c_str());
    if (file.good())
    {
      string moveOutput = "mv " + outputRecoFile + " " + outDir;
      system(moveOutput.c_str());
    }
  }

  delete se;
  std::cout << "Finished" << std::endl;
  gSystem->Exit(0);
}
