/* -*- c++ -*- */
/*
 * Copyright 2013-2017 Nuand LLC
 * Copyright 2013 Dimitri Stolnikov <horiz0n@gmx.net>
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/*
 * config.h is generated by configure.  It contains the results
 * of probing for features, options etc.  It should be the first
 * file included in your .cc file.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <iostream>

#include <boost/assign.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include <gnuradio/io_signature.h>

#include <volk/volk.h>

#include "arg_helpers.h"
#include "bladerf_source_c.h"
#include "osmosdr/source.h"

using namespace boost::assign;

/*
 * Create a new instance of bladerf_source_c and return
 * a boost shared_ptr.  This is effectively the public constructor.
 */
bladerf_source_c_sptr make_bladerf_source_c (const std::string &args)
{
  return gnuradio::get_initial_sptr(new bladerf_source_c (args));
}

/*
 * Specify constraints on number of input and output streams.
 * This info is used to construct the input and output signatures
 * (2nd & 3rd args to gr_block's constructor).  The input and
 * output signatures are used by the runtime system to
 * check that a valid number and type of inputs and outputs
 * are connected to this block.  In this case, we accept either
 * 1 or 2 outputs.
 */
static const int MIN_IN = 0;	// mininum number of input streams
static const int MAX_IN = 0;	// maximum number of input streams
static const int MIN_OUT = 1;	// minimum number of output streams
static const int MAX_OUT = 2;	// maximum number of output streams

/*
 * The private constructor
 */
bladerf_source_c::bladerf_source_c (const std::string &args)
  : gr::sync_block ("bladerf_source_c",
                    gr::io_signature::make (MIN_IN, MAX_IN, sizeof (gr_complex)),
                    gr::io_signature::make (MIN_OUT, MAX_OUT, sizeof (gr_complex)))
{
  int ret;
  std::string device_name;
  struct bladerf_version fpga_version;

  dict_t dict = params_to_dict(args);

  init(dict, BLADERF_RX);

  if (dict.count("sampling"))
  {
    std::string sampling = dict["sampling"];

    std::cerr << _pfx << "Setting bladerf sampling to " << sampling << std::endl;
    if( sampling == "internal") {
      ret = bladerf_set_sampling( _dev.get(), BLADERF_SAMPLING_INTERNAL );
      if ( ret != 0 )
        std::cerr << _pfx << "Problem while setting sampling mode:"
                  << bladerf_strerror(ret) << std::endl;
    } else if( sampling == "external" ) {
      ret = bladerf_set_sampling( _dev.get(), BLADERF_SAMPLING_EXTERNAL );
      if ( ret != 0 )
        std::cerr << _pfx << "Problem while setting sampling mode:"
                  << bladerf_strerror(ret) << std::endl;
    } else {
        std::cerr << _pfx << "Invalid sampling mode " << sampling << std::endl;
    }
  }

  /* Warn user about using an old FPGA version, as we no longer strip off the
   * markers that were pressent in the pre-v0.0.1 FPGA */
  if (bladerf_fpga_version( _dev.get(), &fpga_version ) != 0) {
    std::cerr << _pfx << "Failed to get FPGA version" << std::endl;
  } else if ( fpga_version.major <= 0 &&
              fpga_version.minor <= 0 &&
              fpga_version.patch < 1 ) {

    std::cerr << _pfx << "Warning: FPGA version v0.0.1 or later is required. "
              << "Using an earlier FPGA version will result in misinterpeted samples. "
              << std::endl;
  }
}

bool bladerf_source_c::start()
{
  return bladerf_common::start(BLADERF_RX);
}

bool bladerf_source_c::stop()
{
  return bladerf_common::stop(BLADERF_RX);
}

int bladerf_source_c::work( int noutput_items,
                            gr_vector_const_void_star &input_items,
                            gr_vector_void_star &output_items )
{
  int ret;
  const float scaling = 2048.0f;
  gr_complex *out = static_cast<gr_complex *>(output_items[0]);
  struct bladerf_metadata meta;
  struct bladerf_metadata *meta_ptr = NULL;

  if (noutput_items > _conv_buf_size) {
    void *tmp;

    _conv_buf_size = noutput_items;
    tmp = realloc(_conv_buf, _conv_buf_size * 2 * sizeof(int16_t));
    if (tmp == NULL) {
      throw std::runtime_error( std::string(__FUNCTION__) +
                                "Failed to realloc _conv_buf" );
    }

    _conv_buf = static_cast<int16_t*>(tmp);
  }

  if (_use_metadata) {
    memset(&meta, 0, sizeof(meta));
    meta.flags = BLADERF_META_FLAG_RX_NOW;
    meta_ptr = &meta;
  }

  /* Grab all the samples into the temporary buffer */
  ret = bladerf_sync_rx(_dev.get(), static_cast<void *>(_conv_buf),
                        noutput_items, meta_ptr, _stream_timeout_ms);
  if ( ret != 0 ) {
    std::cerr << _pfx << "bladerf_sync_rx error: "
              << bladerf_strerror(ret) << std::endl;

    _consecutive_failures++;

    if ( _consecutive_failures >= MAX_CONSECUTIVE_FAILURES ) {
        std::cerr << _pfx
                  << "Consecutive error limit hit. Shutting down."
                  << std::endl;
        return WORK_DONE;
    }
  } else {
      _consecutive_failures = 0;
  }

  /* Convert them from fixed to floating point */
  volk_16i_s32f_convert_32f((float*)out, _conv_buf, scaling, 2*noutput_items);

  return noutput_items;
}

std::vector<std::string> bladerf_source_c::get_devices()
{
  return bladerf_common::devices();
}

size_t bladerf_source_c::get_num_channels()
{
  return bladerf_common::get_num_channels(BLADERF_RX);
}

osmosdr::meta_range_t bladerf_source_c::get_sample_rates()
{
  return sample_rates();
}

double bladerf_source_c::set_sample_rate( double rate )
{
  return bladerf_common::set_sample_rate( BLADERF_RX, rate);
}

double bladerf_source_c::get_sample_rate()
{
  return bladerf_common::get_sample_rate( BLADERF_RX );
}

osmosdr::freq_range_t bladerf_source_c::get_freq_range(size_t chan)
{
  return bladerf_common::get_freq_range(BLADERF_CHANNEL_RX(chan));
}

double bladerf_source_c::set_center_freq(double freq, size_t chan)
{
  return bladerf_common::set_center_freq(freq, BLADERF_CHANNEL_RX(chan));
}

double bladerf_source_c::get_center_freq(size_t chan)
{
  return bladerf_common::get_center_freq(BLADERF_CHANNEL_RX(chan));
}

double bladerf_source_c::set_freq_corr( double ppm, size_t chan )
{
  /* TODO: Write the VCTCXO with a correction value (also changes TX ppm value!) */
  return get_freq_corr( BLADERF_CHANNEL_RX(chan) );
}

double bladerf_source_c::get_freq_corr( size_t chan )
{
  /* TODO: Return back the frequency correction in ppm */
  return 0;
}

std::vector<std::string> bladerf_source_c::get_gain_names( size_t chan )
{
  return bladerf_common::get_gain_names(BLADERF_CHANNEL_RX(chan));
}

osmosdr::gain_range_t bladerf_source_c::get_gain_range( size_t chan )
{
  return bladerf_common::get_gain_range(BLADERF_CHANNEL_RX(chan));
}

osmosdr::gain_range_t bladerf_source_c::get_gain_range( const std::string & name, size_t chan )
{
  return bladerf_common::get_gain_range(name, BLADERF_CHANNEL_RX(chan));
}

bool bladerf_source_c::set_gain_mode( bool automatic, size_t chan )
{
  return bladerf_common::set_gain_mode(automatic, BLADERF_CHANNEL_RX(chan));
}

bool bladerf_source_c::get_gain_mode( size_t chan )
{
  return bladerf_common::get_gain_mode(BLADERF_CHANNEL_RX(chan));
}

double bladerf_source_c::set_gain( double gain, size_t chan )
{
  return bladerf_common::set_gain(gain, BLADERF_CHANNEL_RX(chan));
}

double bladerf_source_c::set_gain( double gain, const std::string & name, size_t chan)
{
  return bladerf_common::set_gain(gain, name, BLADERF_CHANNEL_RX(chan));
}

double bladerf_source_c::get_gain( size_t chan )
{
  return bladerf_common::get_gain(BLADERF_CHANNEL_RX(chan));
}

double bladerf_source_c::get_gain( const std::string & name, size_t chan )
{
  return bladerf_common::get_gain(name, BLADERF_CHANNEL_RX(chan));
}

std::vector< std::string > bladerf_source_c::get_antennas( size_t chan )
{
  std::vector< std::string > antennas;

  antennas += "RX0";

  if (BLADERF_REV_2 == get_board_type(_dev.get())) {
    antennas += "RX1";
  }

  return antennas;
}

std::string bladerf_source_c::set_antenna( const std::string & antenna, size_t chan )
{
  return get_antenna( BLADERF_CHANNEL_RX(chan) );
}

std::string bladerf_source_c::get_antenna( size_t chan )
{
  /* We only have a single receive antenna here */
  // TODO: this is a lie
  return "RX0";
}

void bladerf_source_c::set_dc_offset_mode( int mode, size_t chan )
{
  if ( osmosdr::source::DCOffsetOff == mode ) {
    //_src->set_auto_dc_offset( false, chan );
    set_dc_offset( std::complex<double>(0.0, 0.0), chan ); /* reset to default for off-state */
  } else if ( osmosdr::source::DCOffsetManual == mode ) {
    //_src->set_auto_dc_offset( false, chan ); /* disable auto mode, but keep correcting with last known values */
  } else if ( osmosdr::source::DCOffsetAutomatic == mode ) {
    //_src->set_auto_dc_offset( true, chan );
    std::cerr << "Automatic DC correction mode is not implemented." << std::endl;
  }
}

void bladerf_source_c::set_dc_offset( const std::complex<double> &offset, size_t chan )
{
  int ret = 0;

  ret = bladerf_common::set_dc_offset(BLADERF_RX, offset, chan);

  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "could not set dc offset: " +
                              std::string(bladerf_strerror(ret)) );
  }
}

void bladerf_source_c::set_iq_balance_mode( int mode, size_t chan )
{
  if ( osmosdr::source::IQBalanceOff == mode ) {
    //_src->set_auto_iq_balance( false, chan );
    set_iq_balance( std::complex<double>(0.0, 0.0), chan ); /* reset to default for off-state */
  } else if ( osmosdr::source::IQBalanceManual == mode ) {
    //_src->set_auto_iq_balance( false, chan ); /* disable auto mode, but keep correcting with last known values */
  } else if ( osmosdr::source::IQBalanceAutomatic == mode ) {
    //_src->set_auto_iq_balance( true, chan );
    std::cerr << "Automatic IQ correction mode is not implemented." << std::endl;
  }
}

void bladerf_source_c::set_iq_balance( const std::complex<double> &balance, size_t chan )
{
  int ret = 0;

  ret = bladerf_common::set_iq_balance(BLADERF_RX, balance, chan);

  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "could not set iq balance: " +
                              std::string(bladerf_strerror(ret)) );
  }
}

double bladerf_source_c::set_bandwidth( double bandwidth, size_t chan )
{
  int ret;
  uint32_t actual;

  if ( bandwidth == 0.0 ) /* bandwidth of 0 means automatic filter selection */
    bandwidth = get_sample_rate() * 0.75; /* select narrower filters to prevent aliasing */

  ret = bladerf_set_bandwidth( _dev.get(), BLADERF_RX, (uint32_t)bandwidth, &actual );
  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "could not set bandwidth: " +
                              std::string(bladerf_strerror(ret)) );
  }

  return get_bandwidth();
}

double bladerf_source_c::get_bandwidth( size_t chan )
{
  uint32_t bandwidth;
  int ret;

  ret = bladerf_get_bandwidth( _dev.get(), BLADERF_RX, &bandwidth );
  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "could not get bandwidth:" +
                              std::string(bladerf_strerror(ret)) );
  }

  return (double)bandwidth;
}

osmosdr::freq_range_t bladerf_source_c::get_bandwidth_range( size_t chan )
{
  return filter_bandwidths();
}

void bladerf_source_c::set_clock_source(const std::string &source, const size_t mboard)
{
  bladerf_common::set_clock_source(source, mboard);
}

std::string bladerf_source_c::get_clock_source(const size_t mboard)
{
  return bladerf_common::get_clock_source(mboard);
}

std::vector<std::string> bladerf_source_c::get_clock_sources(const size_t mboard)
{
  return bladerf_common::get_clock_sources(mboard);
}
