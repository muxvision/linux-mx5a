#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/tty.h>
#include <linux/usb/option.h>

static const struct snd_soc_dapm_widget phone_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("INPUT_PHONE"),
	SND_SOC_DAPM_INPUT("PCM_INPUT"),
	SND_SOC_DAPM_OUTPUT("SPK_PHONE"),
	SND_SOC_DAPM_OUTPUT("PCM_OUT"),
};

static const struct snd_soc_dapm_route audio_paths[] = {
	{ "SPK_PHONE", NULL, "PCM_INPUT" },
	{ "PCM_OUT", NULL, "INPUT_PHONE" },
};

static int phone_add_widgets(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_new_controls(dapm, phone_dapm_widgets,
			ARRAY_SIZE(phone_dapm_widgets));

	snd_soc_dapm_add_routes(dapm, audio_paths, ARRAY_SIZE(audio_paths));

	return 0;
}

static int phone_set_dai_fmt(struct snd_soc_dai *codec_dai, 
		unsigned int fmt)
{
	if((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBM_CFM)
		return -EINVAL;

	if((fmt & SND_SOC_DAIFMT_FORMAT_MASK) != SND_SOC_DAIFMT_DSP_A)
		return -EINVAL;

	if((fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF)
		return -EINVAL;

	return 0;
}

static int phone_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	snd_pcm_format_t format = params_format(params);

	switch(format) {
		case SNDRV_PCM_FORMAT_S16_LE:
			break;
		default:
			dev_err(codec->dev, "unsupported format %i\n", format);
			return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops phone_dai_ops = {
	.hw_params = phone_hw_params,
	.set_fmt = phone_set_dai_fmt,
};

#define PHONE_RATES		SNDRV_PCM_RATE_8000
#define PHONE_FORMATS	SNDRV_PCM_FMTBIT_S16_LE

static struct snd_soc_dai_driver phone_dai = {
	.name = "phone-pcm",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = PHONE_RATES,
		.formats = PHONE_FORMATS, 
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = PHONE_RATES,
		.formats = PHONE_FORMATS,
	},
	.ops = &phone_dai_ops,
	.symmetric_rates = 1,
};

static int phone_set_bias_level(struct snd_soc_codec *codec, 
		enum snd_soc_bias_level level)
{
	switch(level) {
		case SND_SOC_BIAS_ON:
		case SND_SOC_BIAS_PREPARE:
		case SND_SOC_BIAS_STANDBY:
		case SND_SOC_BIAS_OFF:
			break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

static int phone_suspend(struct snd_soc_codec *codec)
{
	codec->driver->set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int phone_resume(struct snd_soc_codec *codec)
{
	codec->driver->set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}

static int phone_probe(struct snd_soc_codec *codec)
{
	codec->driver->set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	phone_add_widgets(codec);
	return 0;
}

static int phone_remove(struct snd_soc_codec *codec)
{
	codec->driver->set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_phone = {
	.probe = phone_probe,
	.remove = phone_remove,
	.suspend = phone_suspend,
	.resume = phone_resume,
	.set_bias_level = phone_set_bias_level,
};

static int phone_platform_probe(struct platform_device *pdev)
{
	int ret;
	ret = snd_soc_register_codec(&pdev->dev, 
				&soc_codec_dev_phone, &phone_dai, 1);
	return ret;

}

static int phone_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static const struct of_device_id phone_dt_ids[] = {
	{ .compatible = "huawei, 3g_modem", },
	{ }
};
MODULE_DEVICE_TABLE(of, phone_dt_ids);

static struct platform_driver phone_platform_driver = {
	.driver = {
		.name = "phone",
		.owner = THIS_MODULE,
		.of_match_table = phone_dt_ids,
	},
	.probe = phone_platform_probe,
	.remove = phone_platform_remove,
};
//module_platform_driver(phone_platform_driver);


static int __init phone_pcm_init(void)
{
	int ret = -ENODEV;

	if(option_get_huawei_connected())
		ret = platform_driver_register(&phone_platform_driver);
	return ret;
}
//late_initcall(phone_pcm_init);
module_init(phone_pcm_init);

MODULE_AUTHOR("Kashin Yang");
MODULE_DESCRIPTION("Asoc phone pcm codec driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:phone-pcm");

