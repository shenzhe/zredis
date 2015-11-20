<?php
	$h = new Hiredis();
        $h->connect('/tmp/redis.sock');
	$h->set("key", "val");
	echo $h->get("key");
