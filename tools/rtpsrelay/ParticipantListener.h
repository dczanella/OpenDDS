#ifndef RTPSRELAY_PARTICIPANT_LISTENER_H_
#define RTPSRELAY_PARTICIPANT_LISTENER_H_

#include "ListenerBase.h"
#include "DomainStatisticsReporter.h"

#include <dds/DCPS/DomainParticipantImpl.h>
#include <dds/DCPS/PeriodicTask.h>

namespace RtpsRelay {

class ParticipantListener : public ListenerBase {
public:
  ParticipantListener(const Config& config,
                      OpenDDS::DCPS::DomainParticipantImpl* participant,
                      DomainStatisticsReporter& stats_reporter,
                      ParticipantEntryDataWriter_var participant_writer);
  void enable();
  void disable();

private:
  void on_data_available(DDS::DataReader_ptr reader) override;
  void write_sample(const DDS::ParticipantBuiltinTopicData& data,
                    const DDS::SampleInfo& info);
  void unregister_instance(const DDS::SampleInfo& info);
  void unregister();

  class Unregister : public OpenDDS::DCPS::RcObject {
  public:
    Unregister(ParticipantListener& listener);
    void enable();
    void disable();

  private:
    void execute(const OpenDDS::DCPS::MonotonicTimePoint& now);
    typedef OpenDDS::DCPS::PmfPeriodicTask<Unregister> PeriodicTask;
    ParticipantListener& listener_;
    PeriodicTask unregister_task_;
  };

  const Config& config_;
  OpenDDS::DCPS::DomainParticipantImpl* participant_;
  DomainStatisticsReporter& stats_reporter_;
  ParticipantEntryDataWriter_var writer_;
  OpenDDS::DCPS::RcHandle<Unregister> unregister_;
  ACE_Thread_Mutex mutex_;
  std::list<OpenDDS::DCPS::GUID_t> unregister_queue_;
  // mutex_
};

}

#endif // RTPSRELAY_PARTICIPANT_LISTENER_H_
