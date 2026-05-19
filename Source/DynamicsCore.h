#pragma once
#include "FXFunctions.h"

enum class DetectModes
{
    PEAK, MS, RMS
};
struct DetectorParameters
{
    DetectorParameters()= default;
    DetectorParameters& operator = (const DetectorParameters& params)        
    {
        if (this != &params)
        {
            detect_dB = params.detect_dB;
            clamp = params.clamp;
            attack_ms = params.attack_ms;
            release_ms = params.release_ms;
            detect_mode = params.detect_mode;
        }
        return *this;
    }
    DetectModes detect_mode = DetectModes::PEAK;
    double attack_ms = 2.0; 
    double release_ms = 50.0; 
    bool detect_dB = false;
    bool clamp = false; // false = true log detector
};
class AudioDetector{
public:
    AudioDetector() {}
    ~AudioDetector() {}

    bool reset(double _sample_rate)
        {
            setSampleRate(_sample_rate);
            last_env = 0.0;
            return true;
        }

    void setDetectParameters(const DetectorParameters& params)
    {
        m_detect_params = params;
        setAttackTime(m_detect_params.attack_ms, true);
        setReleaseTime(m_detect_params.release_ms, true);
        return;
    }

    DetectorParameters getDetectParameters() const
    {
        return m_detect_params;
    }
    
    double processSample(double xn)
    {
        double input = fabs(xn);
        if (m_detect_params.detect_mode == DetectModes::MS ||
			m_detect_params.detect_mode == DetectModes::RMS)
			input *= input;
        
        double current_env = 0.0;

        if (input > last_env)
            current_env = attack_time * (last_env - input) + input;
        else
            current_env = release_time * (last_env - input) + input;

        // checkFloatUnderflow(current_env);
        
        // --- bound them; can happen when using pre-detector gains of more than 1.0
        if (m_detect_params.clamp)
			current_env = fmin(current_env, 1.0);

		// --- can not be (-)
		current_env = fmax(current_env, 0.0);

		// --- store envelope prior to sqrt for RMS version
		last_env = current_env;

        // --- if RMS, do the SQRT
		if (m_detect_params.detect_mode == DetectModes::RMS)
			current_env = sqrt(current_env);

		// --- if not dB, we are done
		if (!m_detect_params.detect_dB)
			return current_env;

		// --- setup for log( )
		if (current_env <= 0)
		{
			return -96.0;
		}

		// --- log10(x) = log2(x) * log10(2); log2f can use fyl2x uop on x86, faster than log10
		return static_cast<double> (20.0f * std::log2f (static_cast<float> (current_env)) * 0.30103f);
    }
    
    void setSampleRate(double sample_rate)
    {
        if (fs == sample_rate) return;

        fs = sample_rate;

        setAttackTime(m_detect_params.attack_ms, true);
        setReleaseTime(m_detect_params.release_ms, true);
        return;
    }

    // Override the detector's internal envelope to a target dB value. Used by the hold
    // stage so that when hold expires, release continues smoothly from the held peak
    // rather than snapping to whatever the detector decayed to during hold.
    void setLastEnvDb(double db) noexcept
    {
        const double env = std::pow(10.0, db * 0.05);
        last_env = (m_detect_params.detect_mode == DetectModes::RMS) ? (env * env) : env;
    }

protected:
    DetectorParameters m_detect_params;

    double last_env = 0.0;
    double attack_time = 0.0;	///< attack time coefficient
	double release_time = 0.0;	///< release time coefficient
    double fs = 44100;

    /** set our internal atack time coefficients based on times and sample rate */
	void setAttackTime(double attack_in_ms, bool forceCalc = false) 
    {
        if (m_detect_params.attack_ms == attack_in_ms && !forceCalc)
            return;
        
        m_detect_params.attack_ms = std::max(attack_in_ms, 0.001);
        attack_time = exp(TLD_AUDIO_ENVELOPE_ANALOG_TC / (attack_in_ms * std::max(fs, 1.0) * 0.001));
    };

	/** set our internal release time coefficients based on times and sample rate */
	void setReleaseTime(double release_in_ms, bool forceCalc = false)  
    {
        if (m_detect_params.release_ms == release_in_ms && !forceCalc)
            return;
        m_detect_params.release_ms = std::max(release_in_ms, 0.001);
        release_time = exp(TLD_AUDIO_ENVELOPE_ANALOG_TC / (release_in_ms * std::max(fs, 1.0) * 0.001));
    }
};

enum class dynamicsProcessorType { kCompressor, kDownwardExpander };

struct DynamicsProcessorParameters
{
    DynamicsProcessorParameters()= default;
    DynamicsProcessorParameters& operator = (const DynamicsProcessorParameters& params)        
    {
        if (this != &params)
        {
            ratio = params.ratio;
            threshold_dB = params.threshold_dB;
            knee_width_dB = params.knee_width_dB;
            hard_limit_gate = params.hard_limit_gate;
            soft_knee = params.soft_knee;
            enable_sc = params.enable_sc;
            calculation = params.calculation;
            attack_ms = params.attack_ms;
            release_ms = params.release_ms;
            hold_ms = params.hold_ms;
            makeup_gain = params.makeup_gain;
            output_gain_dB = params.output_gain_dB;

            gain_reduction = params.gain_reduction;
            gain_reduction_dB = params.gain_reduction_dB;
        }
        return *this;
    }
    double ratio = 50.0;
    double threshold_dB = -10.0;
    double knee_width_dB = 10.0;
    bool hard_limit_gate = false;
    bool soft_knee = true;
    bool enable_sc = true;
    dynamicsProcessorType calculation = dynamicsProcessorType::kCompressor;
    double attack_ms = 0.0;
    double release_ms = 0.0;
    double hold_ms = 0.0;
    double makeup_gain = 1.0;
    double output_gain_dB = 0.0;

    // for metering
    double gain_reduction = 1.0;
    double gain_reduction_dB = 0.0;
    
};

class DynamicsProcessor
{
public:
    DynamicsProcessor() {}	/* C-TOR */
	~DynamicsProcessor() {}	/* D-TOR */
public:
    bool reset (double _sampleRate)
    {
        sc_xn = 0.0;
        fs_ = _sampleRate;
        m_detector.reset(_sampleRate);
        DetectorParameters detector_params = m_detector.getDetectParameters();
        detector_params.clamp = false;
        detector_params.detect_dB = true;
        m_detector.setDetectParameters(detector_params);
        peak_detect_dB = -200.0;
        hold_counter = 0;
        hold_samples = static_cast<int>(std::lround(std::max(0.0, m_dynamics_params.hold_ms) * fs_ * 0.001));
        return true;
    }

    bool canProcessAudioFrame() { return false; }

    void enableSidechain (bool enableSidechain) { m_dynamics_params.enable_sc = enableSidechain; }

    double processSidechainInputSample (double xn)
    {
        sc_xn = xn;
        return sc_xn;
    }

    DynamicsProcessorParameters getParameters() {return m_dynamics_params;}

    double getGainReductionDb() const noexcept { return m_dynamics_params.gain_reduction_dB; }

    void setParameters(const DynamicsProcessorParameters& _parameters)
    {
        const bool needsCoeffUpdate = (_parameters.attack_ms      != m_dynamics_params.attack_ms     ||
                                       _parameters.release_ms     != m_dynamics_params.release_ms    ||
                                       _parameters.output_gain_dB != m_dynamics_params.output_gain_dB);

        const bool needsHoldUpdate = (_parameters.hold_ms != m_dynamics_params.hold_ms);

        // makeup_gain is computed here, not supplied by caller; save it so the assignment below
        // doesn't clobber it when we skip the recalc (caller's struct always has default 1.0)
        const double savedMakeupGain = m_dynamics_params.makeup_gain;
        m_dynamics_params = _parameters;

        if (needsCoeffUpdate)
        {
            DetectorParameters detector_params = m_detector.getDetectParameters();
            detector_params.attack_ms  = m_dynamics_params.attack_ms;
            detector_params.release_ms = m_dynamics_params.release_ms;
            m_detector.setDetectParameters(detector_params);
            m_dynamics_params.makeup_gain = pow(10.0, m_dynamics_params.output_gain_dB / 20.0);
        }
        else
        {
            m_dynamics_params.makeup_gain = savedMakeupGain;
        }

        if (needsHoldUpdate)
            hold_samples = static_cast<int>(std::lround(std::max(0.0, m_dynamics_params.hold_ms) * fs_ * 0.001));
    }

    double processSample(double xn)
    {
        const double in_for_detect = m_dynamics_params.enable_sc ? sc_xn : xn;
        const double raw_detect_dB = m_detector.processSample(in_for_detect);

        double effective_detect_dB = raw_detect_dB;

        if (hold_samples > 0)
        {
            // Hold the detector at its peak after the signal starts decaying so that
            // gain reduction stays pinned for hold_ms before release begins. Writing
            // peak back into the detector keeps release continuous (no snap at hold end).
            if (raw_detect_dB >= peak_detect_dB)
            {
                peak_detect_dB = raw_detect_dB;
                hold_counter   = hold_samples;
            }
            else if (hold_counter > 0)
            {
                effective_detect_dB = peak_detect_dB;
                m_detector.setLastEnvDb(peak_detect_dB);
                --hold_counter;
            }
            else
            {
                peak_detect_dB = raw_detect_dB;
            }
        }
        else
        {
            peak_detect_dB = raw_detect_dB;
        }

        const double gr = computeGain(effective_detect_dB);
        return xn * gr * m_dynamics_params.makeup_gain;
    }

protected:
    AudioDetector m_detector;
    DynamicsProcessorParameters m_dynamics_params;

    double sc_xn = 0.0;
    double fs_   = 44100.0;
    int    hold_samples   = 0;
    int    hold_counter   = 0;
    double peak_detect_dB = -200.0;

    inline double computeGain(double detect_dB)
    {
        double yn_dB = 0.0;
        if (m_dynamics_params.calculation == dynamicsProcessorType::kCompressor)
		{
			// --- calc gain with knee; force hard-knee when knee_width_dB == 0 to avoid divide-by-zero
			if (m_dynamics_params.soft_knee && m_dynamics_params.knee_width_dB > 0.0)
			{
                // --- left side of knee, outside of width, unity gain zone
				if (2.0*(detect_dB - m_dynamics_params.threshold_dB) < -m_dynamics_params.knee_width_dB)
					yn_dB = detect_dB;
				// --- else inside the knee,
				else if (2.0*(fabs(detect_dB - m_dynamics_params.threshold_dB)) <= m_dynamics_params.knee_width_dB)
				{
					if (m_dynamics_params.hard_limit_gate)	// --- is limiter?
						yn_dB = detect_dB - ((detect_dB - m_dynamics_params.threshold_dB + (m_dynamics_params.knee_width_dB / 2.0)) * (detect_dB - m_dynamics_params.threshold_dB + (m_dynamics_params.knee_width_dB / 2.0))) / (2.0*m_dynamics_params.knee_width_dB);
                    else // --- 2nd order poly



						yn_dB = detect_dB + (((1.0 / m_dynamics_params.ratio) - 1.0) * ((detect_dB - m_dynamics_params.threshold_dB + (m_dynamics_params.knee_width_dB / 2.0)) * (detect_dB - m_dynamics_params.threshold_dB + (m_dynamics_params.knee_width_dB / 2.0)))) / (2.0*m_dynamics_params.knee_width_dB);
				}
				// --- right of knee, compression zone
				else if (2.0*(detect_dB - m_dynamics_params.threshold_dB) > m_dynamics_params.knee_width_dB)
				{
					if (m_dynamics_params.hard_limit_gate) // --- is limiter?
						yn_dB = m_dynamics_params.threshold_dB;
					else
						yn_dB = m_dynamics_params.threshold_dB + (detect_dB - m_dynamics_params.threshold_dB) / m_dynamics_params.ratio;
				}
			}
			else // --- hard knee
			{
				// --- below threshold, unity
				if (detect_dB <= m_dynamics_params.threshold_dB)
					yn_dB = detect_dB;
				else// --- above threshold, compress
				{
					if (m_dynamics_params.hard_limit_gate) // is limiter?
						yn_dB = m_dynamics_params.threshold_dB;
					else
						yn_dB = m_dynamics_params.threshold_dB + (detect_dB - m_dynamics_params.threshold_dB) / m_dynamics_params.ratio;
				}
			}
		}
		else if (m_dynamics_params.calculation == dynamicsProcessorType::kDownwardExpander)
		{
			// --- hard knee; also forced when knee_width_dB == 0 to avoid divide-by-zero
			// --- NOTE: soft knee is not technically possible with a gate because there
			//           is no "left side" of the knee
			if (!m_dynamics_params.soft_knee || m_dynamics_params.hard_limit_gate || m_dynamics_params.knee_width_dB <= 0.0)
			{
				// --- above threshold, unity gain
				if (detect_dB >= m_dynamics_params.threshold_dB)
					yn_dB = detect_dB;
				else
				{
					if (m_dynamics_params.hard_limit_gate) // --- gate: -inf(dB)
						yn_dB = -1.0e34;
					else
						yn_dB = m_dynamics_params.threshold_dB + (detect_dB - m_dynamics_params.threshold_dB) * m_dynamics_params.ratio;
				}
			}
			else // --- calc gain with knee
			{
				// --- right side of knee, unity gain zone
				if (2.0*(detect_dB - m_dynamics_params.threshold_dB) > m_dynamics_params.knee_width_dB)
					yn_dB = detect_dB;
				// --- in the knee
				else if (2.0*(fabs(detect_dB - m_dynamics_params.threshold_dB)) <= m_dynamics_params.knee_width_dB) // Pirkles: > -m_dynamics_params.knee_width_dB
					yn_dB = ((m_dynamics_params.ratio - 1.0) * ((detect_dB - m_dynamics_params.threshold_dB - (m_dynamics_params.knee_width_dB / 2.0)) * (detect_dB - m_dynamics_params.threshold_dB - (m_dynamics_params.knee_width_dB / 2.0)))) / (2.0*m_dynamics_params.knee_width_dB);
				// --- left side of knee, downward expander zone
				else if (2.0*(detect_dB - m_dynamics_params.threshold_dB) <= -m_dynamics_params.knee_width_dB)
					yn_dB = m_dynamics_params.threshold_dB + (detect_dB - m_dynamics_params.threshold_dB) * m_dynamics_params.ratio;
			}
		}

		// --- convert gain; store values for user meters
		// pow(10,x/20) = exp(x*ln10/20); clamp at -6 (≈ -52 dB GR, inaudible) for Padé accuracy
		m_dynamics_params.gain_reduction_dB = yn_dB - detect_dB;
		m_dynamics_params.gain_reduction = static_cast<double> (
            juce::dsp::FastMathApproximations::exp (
                juce::jmax (-6.0f, static_cast<float> (m_dynamics_params.gain_reduction_dB) * 0.11512925f)));

		// --- the current gain coefficient value
		return m_dynamics_params.gain_reduction;
    }

};