//////////////////////////////////////////////////////////////////
// \file     CAFMaker_module.cc
/// \brief   This module creates Common Analysis Files.
//           Inspired by the NOvA CAFMaker package
// \author  $Author: psihas@fnal.gov
//////////////////////////////////////////////////////////////////

// ---------------- TO DO ----------------
//
// - Give real fDet values
// - Clean up code body
// - Add in cycle and batch to params
// - Fill reco tree a bit
// - Move this list some place useful
// - Write insructions
// - Add Truth branch
//
// ---------------------------------------



#include "CAFMakerParams.h"
#include "FillReco.h"
#include "Utils.h"

// C/C++ includes
#include <fenv.h>
#include <time.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef DARWINBUILD
#include <libgen.h>
#endif

#include <IFDH_service.h>

// ROOT includes
#include "TFile.h"
#include "TH1D.h"
#include "TTree.h"
#include "TTimeStamp.h"

// Framework includes
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/FileBlock.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/SubRun.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"

#include "art_root_io/TFileService.h"

#include "canvas/Persistency/Common/FindMany.h"
#include "canvas/Persistency/Common/FindManyP.h"
#include "canvas/Persistency/Common/FindOne.h"
#include "canvas/Persistency/Common/FindOneP.h"
#include "canvas/Persistency/Common/Ptr.h"
#include "canvas/Persistency/Common/PtrVector.h"

#include "cetlib_except/exception.h"

#include "fhiclcpp/ParameterSet.h"

#include "messagefacility/MessageLogger/MessageLogger.h"

// LArSoft includes
#include "lardataobj/RecoBase/Slice.h"

// StandardRecord
#include "sbncafmaker/StandardRecord/StandardRecord.h"

// // CAFMaker 
#include "AssociationUtil.h"
// #include "Blinding.h"

namespace caf {

/// Module to create Common Analysis Files from ART files
class CAFMaker : public art::EDProducer {
 public:
  // Allows 'nova --print-description' to work
  using Parameters = art::EDProducer::Table<CAFMakerParams>;

  explicit CAFMaker(const Parameters& params);
  virtual ~CAFMaker();

  void produce(art::Event& evt) noexcept;

  void respondToOpenInputFile(const art::FileBlock& fb);

  void beginJob();
  void endJob();
  virtual void beginRun(art::Run& r);
  virtual void beginSubRun(art::SubRun& sr);
  virtual void endSubRun(art::SubRun& sr);

 protected:
  CAFMakerParams fParams;

  std::string fCafFilename;

  bool   fIsRealData;  // use this instead of evt.isRealData(), see init in
                     // produce(evt)
  double fTotalPOT;
  double fTotalSinglePOT;
  double fTotalEvents;
  // int fCycle;
  // int fBatch;

  TFile* fFile;
  TTree* fRecTree;

  TH1D* hPOT;
  TH1D* hSinglePOT;
  TH1D* hEvents;

  Det_t fDet;  ///< Detector ID in caf namespace typedef


  void InitializeOutfile();

  /// Equivalent of FindManyP except a return that is !isValid() prints a
  /// messsage and aborts if StrictMode is true.
  template <class T, class U>
  art::FindManyP<T> FindManyPStrict(const U& from, const art::Event& evt,
                                    const art::InputTag& label) const;

  /// \brief Retrieve an object from an association, with error handling
  ///
  /// This can go wrong in two ways: either the FindManyP itself is
  /// invalid, or the result for the requested index is empty. In most
  /// cases these have the same response, so conflating them here
  /// saves redundancy elsewhere.
  ///
  /// \param      fm  The FindManyP object describing the association
  /// \param      idx Which element of the FindManyP to look it
  /// \param[out] ret The product retrieved
  /// \return          Whether \a ret was filled
  template <class T>
  bool GetAssociatedProduct(const art::FindManyP<T>& fm, int idx, T& ret) const;

  /// Equivalent of evt.getByLabel(label, handle) except failedToGet
  /// prints a message and aborts if StrictMode is true.
  template <class T>
  void GetByLabelStrict(const art::Event& evt, const std::string& label,
                        art::Handle<T>& handle) const;

  /// Equivalent of evt.getByLabel(label, handle) except failedToGet
  /// prints a message.
  template <class T>
  void GetByLabelIfExists(const art::Event& evt, const std::string& label,
                          art::Handle<T>& handle) const;

  /// \param      pset The parameter set
  /// \param      name Pass "foo.bar.baz" as {"foo", "bar", "baz"}
  /// \param[out] ret  Value of the key, not set if we return false
  /// \return          Whether the key was found
  template <class T>
  bool GetPsetParameter(const fhicl::ParameterSet& pset,
                        const std::vector<std::string>& name, T& ret) const;

  static bool EssentiallyEqual(double a, double b, double precision = 0.0001) {
    return a <= (b + precision) && a >= (b - precision);
  }

  // static bool sortTrackLength(const SRTrack& a, const SRTrack& b) {
  //   return a.len > b.len;
  // }
//.......................................................................
}; //Producer

//.......................................................................
  CAFMaker::CAFMaker(const Parameters& params) 
  : art::EDProducer{params},
    fParams(params()), fIsRealData(false), fFile(0) {

  fCafFilename = fParams.CAFFilename();

  // Normally CAFMaker is run wit no output ART stream, so these go
  // nowhere, but can be occasionally useful for filtering in ART

  produces<std::vector<caf::StandardRecord>>();
  produces<art::Assns<caf::StandardRecord, recob::Slice>>();
}

//......................................................................
CAFMaker::~CAFMaker() {}

//......................................................................
void CAFMaker::respondToOpenInputFile(const art::FileBlock& fb) {
  if (!fFile) {
    // If Filename wasn't set in the FCL, and this is the
    // first file we've seen
    const int sizef = fb.fileName().size() + 1;
    char* temp  = new char[sizef];
    std::strcpy(temp, fb.fileName().c_str());
    fCafFilename = basename(temp);
    const size_t dotpos = fCafFilename.find('.');
    assert(dotpos != std::string::npos);  // Must have a dot, surely?
    fCafFilename.resize(dotpos);
    fCafFilename += fParams.FileExtension();

    InitializeOutfile();
  }
}

//......................................................................
void CAFMaker::beginJob() {
  if (!fCafFilename.empty()) InitializeOutfile();
}

//......................................................................
void CAFMaker::beginRun(art::Run& r) {
  // fDetID = geom->DetId();
  fDet = (Det_t)1;//(Det_t)fDetID;

}

//......................................................................
void CAFMaker::beginSubRun(art::SubRun& sr) {

}

//......................................................................
void CAFMaker::InitializeOutfile() {
  assert(!fFile);
  assert(!fCafFilename.empty());

  mf::LogInfo("CAFMaker") << "Output filename is " << fCafFilename;

  fFile = new TFile(fCafFilename.c_str(), "RECREATE");

  hPOT = new TH1D("TotalPOT", "TotalPOT;; POT", 1, 0, 1);
  hSinglePOT =
      new TH1D("TotalSinglePOT", "TotalSinglePOT;; Single POT", 1, 0, 1);
  hEvents = new TH1D("TotalEvents", "TotalEvents;; Events", 1, 0, 1);

  fRecTree = new TTree("recTree", "records");

  // Tell the tree it's expecting StandardRecord objects
  StandardRecord* rec = 0;
  fRecTree->Branch("rec", "caf::StandardRecord", &rec);

  fTotalPOT = 0;
  fTotalSinglePOT = 0;
  fTotalEvents = 0;
  // fCycle = -5;
  // fBatch = -5;

  // Global information about the processing details:
  std::map<TString, TString> envmap;
  std::string envstr;
  // Environ comes from unistd.h
  // environ is not present on OSX for some reason, so just use getenv to
  // grab the variables we care about.
#ifdef DARWINBUILD
  std::set<TString> variables;
  variables.insert("USER");
  variables.insert("HOSTNAME");
  variables.insert("PWD");
  variables.insert("SRT_PUBLIC_CONTEXT");
  variables.insert("SRT_PRIVATE_CONTEXT");
  for (auto var : variables) envmap[var] = getenv(var);
#else
  for (char** penv = environ; *penv; ++penv) {
    const std::string pair = *penv;
    envstr += pair;
    envstr += "\n";
    const size_t split = pair.find("=");
    if (split == std::string::npos) continue;  // Huh?
    const std::string key = pair.substr(0, split);
    const std::string value = pair.substr(split + 1);
    envmap[key] = value;
  }
#endif

  // Get the command-line we were invoked with. What I'd really like is
  // just the fcl script and list of input filenames in a less hacky
  // fashion. I'm not sure that's actually possible in ART.
  // TODO: ask the artists.
  FILE* cmdline = fopen("/proc/self/cmdline", "rb");
  char* arg = 0;
  size_t size = 0;
  std::string cmd;
  while (getdelim(&arg, &size, 0, cmdline) != -1) {
    cmd += arg;
    cmd += " ";
  }
  free(arg);
  fclose(cmdline);

  fFile->mkdir("env")->cd();

  TObjString(envmap["USER"]).Write("user");
  TObjString(envmap["HOSTNAME"]).Write("hostname");
  TObjString(envmap["PWD"]).Write("pwd");
  TObjString(envmap["SRT_PUBLIC_CONTEXT"]).Write("publiccontext");
  TObjString(envmap["SRT_PRIVATE_CONTEXT"]).Write("privatecontext");
  // Default constructor is "now"
  TObjString(TTimeStamp().AsString()).Write("date");
  TObjString(cmd.c_str()).Write("cmd");
  TObjString(fCafFilename.c_str()).Write("output");
  TObjString(envstr.c_str()).Write("env");
}

//......................................................................
template <class T, class U>
art::FindManyP<T> CAFMaker::FindManyPStrict(const U& from,
                                            const art::Event& evt,
                                            const art::InputTag& tag) const {
  art::FindManyP<T> ret(from, evt, tag);

  if (!tag.label().empty() && !ret.isValid() && fParams.StrictMode()) {
    std::cout << "CAFMaker: No Assn from '"
              << abi::__cxa_demangle(typeid(from).name(), 0, 0, 0) << "' to '"
              << abi::__cxa_demangle(typeid(T).name(), 0, 0, 0)
              << "' found under label '" << tag << "'. "
              << "Set 'StrictMode: false' to continue anyway." << std::endl;
    abort();
  }

  return ret;
}

//......................................................................
template <class T>
bool CAFMaker::GetAssociatedProduct(const art::FindManyP<T>& fm, int idx,
                                    T& ret) const {
  if (!fm.isValid()) return false;

  const std::vector<art::Ptr<T>> prods = fm.at(idx);

  if (prods.empty()) return false;

  ret = *prods[0];

  return true;
}

//......................................................................
template <class T>
void CAFMaker::GetByLabelStrict(const art::Event& evt, const std::string& label,
                                art::Handle<T>& handle) const {
  evt.getByLabel(label, handle);
  if (!label.empty() && handle.failedToGet() && fParams.StrictMode()) {
    std::cout << "CAFMaker: No product of type '"
              << abi::__cxa_demangle(typeid(*handle).name(), 0, 0, 0)
              << "' found under label '" << label << "'. "
              << "Set 'StrictMode: false' to continue anyway." << std::endl;
    abort();
  }
}

//......................................................................
template <class T>
void CAFMaker::GetByLabelIfExists(const art::Event& evt,
                                  const std::string& label,
                                  art::Handle<T>& handle) const {
  evt.getByLabel(label, handle);
  if (!label.empty() && handle.failedToGet() && fParams.StrictMode()) {
    std::cout << "CAFMaker: No product of type '"
              << abi::__cxa_demangle(typeid(*handle).name(), 0, 0, 0)
              << "' found under label '" << label << "'. "
              << "Continuing without it." << std::endl;
  }
}

//......................................................................
template <class T>
bool CAFMaker::GetPsetParameter(const fhicl::ParameterSet& pset,
                                const std::vector<std::string>& name,
                                T& ret) const {
  fhicl::ParameterSet p = pset;
  for (unsigned int i = 0; i < name.size() - 1; ++i) {
    if (!p.has_key(name[i])) return false;
    p = p.get<fhicl::ParameterSet>(name[i]);
  }
  if (!p.has_key(name.back())) return false;
  ret = p.get<T>(name.back());
  return true;
}

//......................................................................
void CAFMaker::produce(art::Event& evt) noexcept {

  std::unique_ptr<std::vector<caf::StandardRecord>> srcol(
      new std::vector<caf::StandardRecord>);

  std::unique_ptr<art::Assns<caf::StandardRecord, recob::Slice>> srAssn(
      new art::Assns<caf::StandardRecord, recob::Slice>);

  fTotalEvents += 1;

  // Get a handle on the slices
  art::Handle<std::vector<recob::Slice>> slices;
  GetByLabelStrict(evt, fParams.ClusterLabel(), slices);

  // Get tracks & showers here

  for (unsigned int sliceId = 0; sliceId < slices->size(); ++sliceId) {
    const recob::Slice& slice = (*slices)[sliceId];

    //    if (slice.IsNoise() || slice.NCell() == 0) continue;
    // Because we don't care about the noise slice and slices with no hits.
    StandardRecord rec;
    StandardRecord* prec = &rec;  // TTree wants a pointer-to-pointer
    fRecTree->SetBranchAddress("rec", &prec);

    //#######################################################
    // Fill slice header.
    //#######################################################
    // Get metadata information for header
    unsigned int run = evt.run();
    unsigned int subrun = evt.subRun();
    //   unsigned int spillNum = evt.id().event();

    rec.hdr = SRHeader();

    rec.hdr.run = run;
    rec.hdr.subrun = subrun;
    rec.hdr.subevt = sliceId;
    rec.hdr.ismc = !fIsRealData;
    rec.hdr.det = fDet;
    // rec.hdr.cycle = fCycle;
    // rec.hdr.batch = fBatch;
    // rec.hdr.blind = 0;
    // rec.hdr.filt = rb::IsFiltered(evt, slices, sliceId);

    //#######################################################
    // Add slice info.
    //#######################################################
    FillSliceVars(slice, rec.slc);

    //#######################################################
    // Add detector dependent slice info.
    //#######################################################
    // if (fDet == kSBND) {
    //   rec.sel.contain.nplanestofront = rec.slc.firstplane - (plnfirst - 1);
    //   rec.sel.contain.nplanestoback = (plnlast) - 1 - rec.slc.lastplane;
    // }

    //#######################################################
    // Add reconstructed objects.
    //#######################################################

    // if (fmTrack.isValid()) {
    //   std::vector<art::Ptr<rb::Track>> slcTracks = fmTrack.at(sliceId);
    //   // Sort the tracks in descending length order
    //   std::sort(kalTracks.begin(), kalTracks.end(), sortRBTrackLength);
    //   rec.reco.trk.idxlongest = 0;

    //   for (size_t trackId = 0; trackId < slcTracks.size(); ++trackId) {

    //       rec.reco.trk.tracks.push_back(SRKalmanTrack());

    //       FillTrackVars((*slcTracks[trackId]), rec.reco.trk.tracks.back(),
    //                      sliceId);

    //     }// end for trackId
    //   } // fmTrack ok

    //#######################################################
    // Fill truth information
    //#######################################################

    // // Set mc branch values to default
    // rec.mc.setDefault();
    // if (fParams.EnableBlindness()) BlindThisRecord(&rec);

    fRecTree->Fill();
    srcol->push_back(rec);
    util::CreateAssn(*this, evt, *srcol, art::Ptr<recob::Slice>(slices, sliceId),
                     *srAssn);
  }  // end loop over slices

  evt.put(std::move(srcol));
}

void CAFMaker::endSubRun(art::SubRun& sr) {

}

//......................................................................
void CAFMaker::endJob() {
  if (fTotalEvents == 0) {

    std::cerr << "No events processed in this file. Aborting rather than "
                 "produce an empty CAF."
              << std::endl;
    abort();
  }

  // Make sure the recTree is in the file before filling other items 
  // for debugging. 
  fFile->Write();

  fFile->cd();
  hEvents->Fill(.5, fTotalEvents);

  hEvents->Write();
  fFile->Write();

}

DEFINE_ART_MODULE(CAFMaker)

}  // end namespace caf
////////////////////////////////////////////////////////////////////////
