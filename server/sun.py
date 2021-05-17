from astral import Astral
from astral import AstralError
from croniter import croniter
from datetime import datetime
from datetime import timedelta
from logging import info

from firestore import DataError
from geocoder import GeocoderWrapper
from local_time import LocalTime


class Sun(object):
    """A wrapper around a calculator for sunrise and sunset times."""

    def __init__(self, geocoder):
        """Sun class constructor.

        Args:
            gecoder (gecoder.Geocoder): Used to localize user specified locations
        """
        self._astral = Astral(geocoder=GeocoderWrapper, wrapped=geocoder)
        self._local_time = LocalTime(geocoder)

    def rewrite_cron(self, cron, reference, user):
        """Replace references to sunrise and sunset in a cron expression."""
        # Skip if there is nothing to rewrite.
        if 'sunrise' not in cron and 'sunset' not in cron:
            return cron

        # Replace H:M:S in the reference time so that we get the correct sunrise / set
        reference_midnight = reference.replace(hour=0, minute=0, second=0, microsecond=0)

        # Determine the first two days of the cron expression after the
        # reference, which covers all candidate sunrises and sunsets.
        yesterday = reference_midnight - timedelta(days=1)
        midnight_cron = cron.replace('sunrise', '0 0').replace('sunset', '0 0')
        try:
            first_day = croniter(midnight_cron, yesterday).get_next(datetime)
            second_day = croniter(midnight_cron, first_day).get_next(datetime)
        except ValueError as e:
            raise DataError(e)

        zone = self._local_time.zone(user)
        try:
            home = self._astral[user.get('home')]
        except (AstralError, KeyError) as e:
            raise DataError(e)

        # Calculate the closest future sunrise time and replace the term in the
        # cron expression with minutes and hours.
        if 'sunrise' in cron:
            sunrises = map(lambda x: home.sunrise(x).astimezone(zone),
                           [first_day, second_day])
            next_sunrise = min(filter(lambda x: x >= reference_midnight, sunrises))
            sunrise_cron = cron.replace('sunrise', '%d %d' % (
                next_sunrise.minute, next_sunrise.hour))
            info('Rewrote cron: (%s) -> (%s), reference %s' % (
                cron,
                sunrise_cron,
                reference_midnight.strftime('%A %B %d %Y %H:%M:%S %Z')))
            return sunrise_cron

        # Calculate the closest future sunset time and replace the term in the
        # cron expression with minutes and hours.
        if 'sunset' in cron:
            sunsets = map(lambda x: home.sunset(x).astimezone(zone),
                          [first_day, second_day])
            next_sunset = min(filter(lambda x: x >= reference_midnight, sunsets))
            sunset_cron = cron.replace('sunset', '%d %d' % (next_sunset.minute,
                                                            next_sunset.hour))
            info('Rewrote cron: (%s) -> (%s), reference %s' % (
                cron,
                sunset_cron,
                reference_midnight.strftime('%A %B %d %Y %H:%M:%S %Z')))
            return sunset_cron

    def is_daylight(self, user):
        """Calculate whether the sun is currently up."""
        # Find the sunrise and sunset times for today.
        time = self._local_time.now(user)
        zone = self._local_time.zone(user)
        try:
            home = self._astral[user.get('home')]
        except (AstralError, KeyError) as e:
            raise DataError(e)
        sunrise = home.sunrise(time).astimezone(zone)
        sunset = home.sunset(time).astimezone(zone)

        is_daylight = time > sunrise and time < sunset

        info('Daylight: %s (%s)' % (is_daylight,
                                    time.strftime('%A %B %d %Y %H:%M:%S %Z')))

        return is_daylight
