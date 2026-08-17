#include <linux/input.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <linux/delay.h>

struct udt_joystick {
	struct input_dev *idev;
	struct serdev_device *serdev;
	u8 packet[8];
	int idx;
};

static size_t udt_joystick_receive_buf(struct serdev_device *serdev, const unsigned char *buf, size_t count)
{
	struct udt_joystick *joy = serdev_device_get_drvdata(serdev);
	size_t i;

	for (i = 0; i < count; i++) {
		joy->packet[joy->idx] = buf[i];

		/* Sync Byte 0 */
		if (joy->idx == 0 && joy->packet[0] != 0xA7)
			continue;
		
		/* Sync Byte 1 */
		if (joy->idx == 1 && joy->packet[1] != 0x10) {
			joy->idx = 0;
			continue;
		}

		joy->idx++;

		/* Full Packet Received */
		if (joy->idx == 8) {
			if (joy->packet[7] == 0x00) {
				
				/* Left Stick is physically inverted. */
				input_report_abs(joy->idev, ABS_X,  255 - joy->packet[3]);
				input_report_abs(joy->idev, ABS_Y,  255 - joy->packet[4]);
				
				/* Right Stick is wired correctly */
				input_report_abs(joy->idev, ABS_RX, joy->packet[5]);
				input_report_abs(joy->idev, ABS_RY, joy->packet[6]);
				
				input_sync(joy->idev);
			}
			joy->idx = 0; /* Reset for next packet */
		}
	}
	return count;
}

static const struct serdev_device_ops udt_joystick_ops = {
	.receive_buf = udt_joystick_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

static int udt_joystick_probe(struct serdev_device *serdev)
{
	struct udt_joystick *joy;
	int err;
	u8 auth_init[] = {0xA6, 0x01, 0x00, 0x11, 0x22, 0x33}; /* auth packet */
	u8 auth_keepalive[] = {0xA6, 0x01, 0x01, 0x01, 0x01, 0x01}; /* keepalive packet */

	joy = devm_kzalloc(&serdev->dev, sizeof(*joy), GFP_KERNEL);
	if (!joy)
		return -ENOMEM;

	joy->serdev = serdev;
	serdev_device_set_drvdata(serdev, joy);
	serdev_device_set_client_ops(serdev, &udt_joystick_ops);

	err = serdev_device_open(serdev);
	if (err) {
		dev_err(&serdev->dev, "Unable to open serial device\n");
		return err;
	}

	serdev_device_set_baudrate(serdev, 9600);
	serdev_device_set_flow_control(serdev, false);

	joy->idev = devm_input_allocate_device(&serdev->dev);
	if (!joy->idev) {
		err = -ENOMEM;
		goto err_close;
	}

	joy->idev->name = "GA36-MB Serial Joysticks";
	joy->idev->id.bustype = BUS_HOST;

	input_set_abs_params(joy->idev, ABS_X,  0, 255, 15, 2);
	input_set_abs_params(joy->idev, ABS_Y,  0, 255, 15, 2);
	input_set_abs_params(joy->idev, ABS_RX, 0, 255, 15, 2);
	input_set_abs_params(joy->idev, ABS_RY, 0, 255, 15, 2);

	err = input_register_device(joy->idev);
	if (err) {
		dev_err(&serdev->dev, "Failed to register input device\n");
		goto err_close;
	}

	/* Send auth packet and keepalive to the coprocessor MCU */
	serdev_device_write_buf(serdev, auth_init, sizeof(auth_init));
	msleep(100);
	serdev_device_write_buf(serdev, auth_keepalive, sizeof(auth_keepalive));

	dev_info(&serdev->dev, "GA36-MB Serial Joystick Driver Initialized Successfully\n");
	return 0;

err_close:
	serdev_device_close(serdev);
	return err;
}

static void udt_joystick_remove(struct serdev_device *serdev)
{
	serdev_device_close(serdev);
}

static const struct of_device_id udt_joystick_of_match[] = {
	{ .compatible = "ga36mb,serial-joysticks" },
	{ }
};
MODULE_DEVICE_TABLE(of, udt_joystick_of_match);

static struct serdev_device_driver udt_joystick_driver = {
	.driver = {
		.name = "serial-joysticks",
		.of_match_table = udt_joystick_of_match,
	},
	.probe = udt_joystick_probe,
	.remove = udt_joystick_remove,
};
module_serdev_device_driver(udt_joystick_driver);

MODULE_AUTHOR("Jeremy Clark");
MODULE_DESCRIPTION("GA36-MB Serial Joystick Driver");
MODULE_LICENSE("GPL");