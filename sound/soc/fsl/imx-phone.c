
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>

#include "imx-audmux.h"
#include <linux/usb/option.h>

#define DAI_NAME_SIZE	32

struct imx_phone_data {
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
	char codec_dai_name[DAI_NAME_SIZE];
	char platform_name[DAI_NAME_SIZE];
};


struct imx_priv {
	struct snd_soc_codec *codec;
	struct platform_device *pdev;
	struct snd_pcm_substream *first_stream;
	struct snd_pcm_substream *second_stream;
};

static struct imx_priv card_priv;

static const struct snd_soc_dapm_widget imx_phone_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Phone Spk", NULL),
	SND_SOC_DAPM_MIC("Phone Mic", NULL),
};

static int imx_pcm_hw_params(struct snd_pcm_substream *substream, 
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct imx_priv *priv = &card_priv;
	struct device *dev = &priv->pdev->dev;
	u32 dai_format;
	int ret;

	if(!priv->first_stream)
		priv->first_stream = substream;
	else {
		priv->second_stream = substream;
		return 0;
	}

	dai_format = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM;

	ret = snd_soc_dai_set_fmt(codec_dai, dai_format);
	if(ret)
		dev_err(dev, "failed to set codec dai fmt: %d\n", ret);
	return ret;
}

static struct snd_soc_ops imx_phone_ops = {
	.hw_params = imx_pcm_hw_params,
};
int get_phone_card_connected(void);

static int imx_phone_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cpu_np, *codec_np;
	struct platform_device *cpu_pdev;
	struct imx_priv *priv = &card_priv;
	struct imx_phone_data *data;
	int int_port, ext_port;
	int ret;

	if(!option_get_huawei_connected())
		return -ENODEV;
	priv->pdev = pdev;

	cpu_np = of_parse_phandle(pdev->dev.of_node, "cpu-dai", 0);
	if(!cpu_np) {
		dev_err(&pdev->dev, "cpu dai phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	if(!strstr(cpu_np->name, "ssi"))
		goto audmux_bypass;

	ret = of_property_read_u32(np, "mux-int-port", &int_port);
	if(ret) {
		dev_err(&pdev->dev, "mux-int-port missing or invalid\n");
		return ret;
	}
	ret = of_property_read_u32(np, "mux-ext-port", &ext_port);
	if(ret) {
		dev_err(&pdev->dev, "mux-ext-port missing or invalid\n");
		return ret;
	}

	int_port--;
	ext_port--;
	ret = imx_audmux_v2_configure_port(int_port, 
			IMX_AUDMUX_V2_PTCR_SYN |
			IMX_AUDMUX_V2_PTCR_TFSEL(ext_port) |
			IMX_AUDMUX_V2_PTCR_TCSEL(ext_port) |
			IMX_AUDMUX_V2_PTCR_TFSDIR |
			IMX_AUDMUX_V2_PTCR_TCLKDIR,
			IMX_AUDMUX_V2_PDCR_RXDSEL(ext_port));
	if(ret) {
		dev_err(&pdev->dev, "audmux internal port setup failed\n");
		return ret;
	}
	imx_audmux_v2_configure_port(ext_port, 
			IMX_AUDMUX_V2_PTCR_SYN,
			IMX_AUDMUX_V2_PDCR_RXDSEL(int_port));
	if(ret) {
		dev_err(&pdev->dev, "audmux external port setup failed\n");
		return ret;
	}

audmux_bypass:
	codec_np = of_parse_phandle(pdev->dev.of_node, "audio-codec", 0);
	if(!codec_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_pdev = of_find_device_by_node(cpu_np);
	if(!cpu_pdev) {
		dev_err(&pdev->dev, "failed to find SSI platform device");
		ret = -EINVAL;
		goto fail;
	}

	priv->first_stream = NULL;
	priv->second_stream = NULL;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if(!data) {
		ret = -ENOMEM;
		goto fail;
	}

	data->dai.name = "PCM";
	data->dai.stream_name = "PCM";
	data->dai.codec_dai_name = "phone-pcm";
	data->dai.codec_of_node = codec_np;
	data->dai.cpu_dai_name = dev_name(&cpu_pdev->dev);
	data->dai.platform_of_node = cpu_np;
	data->dai.ops = &imx_phone_ops;
	data->dai.dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM;

	data->card.dev = &pdev->dev;
	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if(ret)
		goto fail;
	ret = snd_soc_of_parse_audio_routing(&data->card, "audio-routing");
	if(ret)
		goto fail;
	data->card.num_links = 1;
	data->card.dai_link = &data->dai;
	data->card.dapm_widgets = imx_phone_dapm_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(imx_phone_dapm_widgets);

	platform_set_drvdata(pdev, &data->card);
	snd_soc_card_set_drvdata(&data->card, data);

	ret = snd_soc_register_card(&data->card);
	if(ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
	}

fail:
	if(cpu_np)
		of_node_put(cpu_np);
	if(codec_np)
		of_node_put(codec_np);

	return ret;
}

static int imx_phone_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static const struct of_device_id imx_phone_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-phone", },
	{ }
};
MODULE_DEVICE_TABLE(of, imx_phone_dt_ids);

static struct platform_driver imx_phone_driver = {
	.driver = {
		.name = "imx-phone",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_phone_dt_ids,
	},
	.probe = imx_phone_probe,
	.remove = imx_phone_remove,
};
//module_platform_driver(imx_phone_driver);

static int __init imx_phone_init(void)
{
	return platform_driver_register(&imx_phone_driver);
}
//late_initcall(imx_phone_init);
module_init(imx_phone_init);

MODULE_AUTHOR("Kashin Yang");
MODULE_DESCRIPTION("Muxvision i.MX PCM phone Asoc machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-phone");



